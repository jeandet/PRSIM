#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/model_app.hpp>
#include <prism/app/headless_window.hpp>
#include <prism/core/field.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

struct IdleModel {
    prism::Field<int> value{0};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); }
};

TEST_CASE("post from worker triggers publish with zero input (idle wake)") {
    std::shared_ptr<const prism::SceneSnapshot> latest;
    std::atomic<size_t> submit_count{0};
    std::atomic<prism::AppContext*> ctx_ptr{nullptr};
    IdleModel model;
    IdleModel* model_ptr = &model;

    struct Backend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest_ref;
        std::atomic<size_t>& count;
        std::atomic<prism::AppContext*>& ctx_ref;
        IdleModel* mptr;
        prism::HeadlessWindow window_{0, {}};
        Backend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c,
                std::atomic<prism::AppContext*>& p, IdleModel* m)
            : latest_ref(l), count(c), ctx_ref(p), mptr(m) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            while (!ctx_ref.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto* ctx = ctx_ref.load(std::memory_order_acquire);
            // post from a worker thread (not logic thread) — must enqueue + wake
            std::thread worker([ctx, this] {
                ctx->post([this] { mptr->value.set(99); });
            });
            worker.join();
            auto before = count.load(std::memory_order_acquire);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (count.load(std::memory_order_acquire) == before
                   && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest_ref = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    auto backend = prism::Backend{std::make_unique<Backend>(latest, submit_count, ctx_ptr, model_ptr)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        ctx_ptr.store(&ctx, std::memory_order_release);
    });
    CHECK(model.value.get() == 99);
    CHECK(submit_count.load() >= 2);
}

TEST_CASE("posts are FIFO per producer") {
    IdleModel model;
    std::atomic<size_t> submit_count{0};
    std::vector<int> order;
    std::mutex order_mu;

    struct Backend final : public prism::BackendBase {
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        Backend(std::atomic<size_t>& c) : count(c) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    auto backend = prism::Backend{std::make_unique<Backend>(submit_count)};
    auto& window = backend.create_window({});
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        std::thread t([&ctx, &order, &order_mu] {
            for (int i = 0; i < 5; ++i) {
                ctx.post([i, &order, &order_mu] {
                    std::lock_guard<std::mutex> lk(order_mu);
                    order.push_back(i);
                });
            }
        });
        t.join();
        // direct dispatch from logic thread should run inline
        ctx.post([&order, &order_mu] {
            std::lock_guard<std::mutex> lk(order_mu);
            order.push_back(99);
        });
    });
    CHECK(order.size() == 6);
    if (order.size() == 6) {
        auto pos = [&](int v) -> size_t {
            for (size_t i = 0; i < order.size(); ++i) if (order[i] == v) return i;
            return size_t(-1);
        };
        CHECK(pos(0) < pos(1));
        CHECK(pos(1) < pos(2));
        CHECK(pos(2) < pos(3));
        CHECK(pos(3) < pos(4));
    }
}

TEST_CASE("concurrent posts from N threads all run") {
    IdleModel model;
    std::atomic<size_t> submit_count{0};
    std::atomic<int> run_count{0};

    struct Backend final : public prism::BackendBase {
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        Backend(std::atomic<size_t>& c) : count(c) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    auto backend = prism::Backend{std::make_unique<Backend>(submit_count)};
    auto& window = backend.create_window({});
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        std::vector<std::thread> workers;
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&ctx, &run_count] {
                for (int j = 0; j < 10; ++j) ctx.post([&run_count] { run_count.fetch_add(1); });
            });
        }
        for (auto& t : workers) t.join();
    });
    CHECK(run_count.load() == 40);
}

TEST_CASE("post after close is dropped (no UAF)") {
    IdleModel model;
    std::atomic<size_t> submit_count{0};
    std::atomic<prism::AppContext*> ctx_ptr{nullptr};
    std::atomic<int> run_count{0};

    struct Backend final : public prism::BackendBase {
        std::atomic<size_t>& count;
        std::atomic<prism::AppContext*>& ctx_ref;
        std::atomic<int>& rc;
        prism::HeadlessWindow window_{0, {}};
        Backend(std::atomic<size_t>& c, std::atomic<prism::AppContext*>& p, std::atomic<int>& r)
            : count(c), ctx_ref(p), rc(r) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
            // After close, logic thread has called loop.finish() and set closed flag.
            // Give it a moment then try to post — must be dropped, not UAF.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (auto* ctx = ctx_ref.load(std::memory_order_acquire)) {
                ctx->post([&] { rc.fetch_add(1); });
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    auto backend = prism::Backend{std::make_unique<Backend>(submit_count, ctx_ptr, run_count)};
    auto& window = backend.create_window({});
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        ctx_ptr.store(&ctx, std::memory_order_release);
    });
    CHECK(run_count.load() == 0);
}
