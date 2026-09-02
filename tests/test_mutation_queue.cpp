#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/model_app.hpp>
#include <prism/app/headless_window.hpp>
#include <prism/core/field.hpp>
#include <prism/core/error_hub.hpp>

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
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

TEST_CASE("throwing posted closure does not wedge post and is reported once") {
    IdleModel model;
    std::atomic<size_t> submit_count{0};
    std::atomic<prism::AppContext*> ctx_ptr{nullptr};
    std::atomic<int> flag{0};
    std::atomic<int> handler_calls{0};
    std::mutex what_mu;
    std::string captured_what;

    prism::core::set_unhandled_error_handler([&](std::exception_ptr eptr) {
        handler_calls.fetch_add(1, std::memory_order_relaxed);
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(what_mu);
            captured_what = e.what();
        }
    });

    struct Backend final : public prism::BackendBase {
        std::atomic<size_t>& count;
        std::atomic<prism::AppContext*>& ctx_ref;
        std::atomic<int>& flag_ref;
        prism::HeadlessWindow window_{0, {}};
        Backend(std::atomic<size_t>& c, std::atomic<prism::AppContext*>& p, std::atomic<int>& f)
            : count(c), ctx_ref(p), flag_ref(f) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            while (!ctx_ref.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto* ctx = ctx_ref.load(std::memory_order_acquire);

            std::thread worker([ctx] {
                ctx->post([] { throw std::runtime_error("boom"); });
                ctx->post([&] {});
            });
            worker.join();

            std::thread flag_setter([ctx, this] {
                ctx->post([this] { flag_ref.store(1, std::memory_order_release); });
            });
            flag_setter.join();

            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (flag_ref.load(std::memory_order_acquire) == 0
                   && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));

            // Prove scheduled_ was reset after the throwing drain: a fresh post
            // from the same producer thread must still be executed.
            std::thread again([ctx, this] {
                ctx->post([this] { flag_ref.fetch_add(1, std::memory_order_release); });
            });
            again.join();

            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (flag_ref.load(std::memory_order_acquire) < 2
                   && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));

            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    auto backend = prism::Backend{std::make_unique<Backend>(submit_count, ctx_ptr, flag)};
    auto& window = backend.create_window({});
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        ctx_ptr.store(&ctx, std::memory_order_release);
    });

    CHECK(flag.load() == 2);
    CHECK(handler_calls.load() == 1);
    {
        std::lock_guard<std::mutex> lk(what_mu);
        CHECK(captured_what == "boom");
    }

    prism::core::set_unhandled_error_handler(nullptr);
}

TEST_CASE("set_unhandled_error_handler(nullptr) restores the default handler") {
    prism::core::set_unhandled_error_handler([](std::exception_ptr) {});
    prism::core::set_unhandled_error_handler(nullptr);
    try {
        throw std::runtime_error("default handler smoke test");
    } catch (...) {
        prism::core::report_unhandled_error(std::current_exception());
    }
}
