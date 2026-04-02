#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <cstdio>
#include <sstream>
#include <string>

namespace prism {
namespace detail {

inline std::string fmt_float(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

inline std::string fmt_color(Color c) {
    return "rgba(" + std::to_string(c.r) + "," + std::to_string(c.g) + ","
         + std::to_string(c.b) + "," + fmt_float(c.a / 255.0f) + ")";
}

inline std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
        case '&':  out += "&amp;"; break;
        case '<':  out += "&lt;"; break;
        case '>':  out += "&gt;"; break;
        case '"':  out += "&quot;"; break;
        default:   out += ch; break;
        }
    }
    return out;
}

struct SvgEmitter {
    std::ostringstream out;
    int clip_id = 0;

    void emit(const FilledRect& c) {
        out << "<rect x=\"" << fmt_float(c.rect.origin.x.raw())
            << "\" y=\"" << fmt_float(c.rect.origin.y.raw())
            << "\" width=\"" << fmt_float(c.rect.extent.w.raw())
            << "\" height=\"" << fmt_float(c.rect.extent.h.raw())
            << "\" fill=\"" << fmt_color(c.color) << "\"/>\n";
    }

    void emit(const RectOutline& c) {
        out << "<rect x=\"" << fmt_float(c.rect.origin.x.raw())
            << "\" y=\"" << fmt_float(c.rect.origin.y.raw())
            << "\" width=\"" << fmt_float(c.rect.extent.w.raw())
            << "\" height=\"" << fmt_float(c.rect.extent.h.raw())
            << "\" stroke=\"" << fmt_color(c.color)
            << "\" fill=\"none\""
            << " stroke-width=\"" << fmt_float(c.thickness) << "\"/>\n";
    }

    void emit(const RoundedRect& c) {
        out << "<rect x=\"" << fmt_float(c.rect.origin.x.raw())
            << "\" y=\"" << fmt_float(c.rect.origin.y.raw())
            << "\" width=\"" << fmt_float(c.rect.extent.w.raw())
            << "\" height=\"" << fmt_float(c.rect.extent.h.raw())
            << "\" rx=\"" << fmt_float(c.radius) << "\"";
        if (c.thickness > 0.f) {
            out << " stroke=\"" << fmt_color(c.color)
                << "\" fill=\"none\""
                << " stroke-width=\"" << fmt_float(c.thickness) << "\"";
        } else {
            out << " fill=\"" << fmt_color(c.color) << "\"";
        }
        out << "/>\n";
    }

    void emit(const TextCmd& c) {
        out << "<text x=\"" << fmt_float(c.origin.x.raw())
            << "\" y=\"" << fmt_float(c.origin.y.raw())
            << "\" font-family=\"monospace\""
            << " font-size=\"" << fmt_float(c.size)
            << "\" fill=\"" << fmt_color(c.color) << "\"";
        if (c.anchor == TextAnchor::Center)
            out << " text-anchor=\"middle\" dominant-baseline=\"central\"";
        if (c.angle != 0.f)
            out << " transform=\"rotate(" << fmt_float(-c.angle)
                << " " << fmt_float(c.origin.x.raw())
                << " " << fmt_float(c.origin.y.raw()) << ")\"";
        out << ">" << xml_escape(c.text) << "</text>\n";
    }

    void emit(const Line& c) {
        out << "<line x1=\"" << fmt_float(c.from.x.raw())
            << "\" y1=\"" << fmt_float(c.from.y.raw())
            << "\" x2=\"" << fmt_float(c.to.x.raw())
            << "\" y2=\"" << fmt_float(c.to.y.raw())
            << "\" stroke=\"" << fmt_color(c.color)
            << "\" stroke-width=\"" << fmt_float(c.thickness) << "\"/>\n";
    }

    void emit(const Polyline& c) {
        out << "<polyline points=\"";
        for (std::size_t i = 0; i < c.points.size(); ++i) {
            if (i > 0) out << " ";
            out << fmt_float(c.points[i].x.raw()) << "," << fmt_float(c.points[i].y.raw());
        }
        out << "\" stroke=\"" << fmt_color(c.color)
            << "\" fill=\"none\""
            << " stroke-width=\"" << fmt_float(c.thickness) << "\"/>\n";
    }

    void emit(const Circle& c) {
        out << "<circle cx=\"" << fmt_float(c.center.x.raw())
            << "\" cy=\"" << fmt_float(c.center.y.raw())
            << "\" r=\"" << fmt_float(c.radius) << "\"";
        if (c.thickness > 0.f) {
            out << " stroke=\"" << fmt_color(c.color)
                << "\" fill=\"none\""
                << " stroke-width=\"" << fmt_float(c.thickness) << "\"";
        } else {
            out << " fill=\"" << fmt_color(c.color) << "\"";
        }
        out << "/>\n";
    }

    void emit(const ClipPush& c) {
        int id = clip_id++;
        out << "<clipPath id=\"clip-" << id << "\">"
            << "<rect x=\"" << fmt_float(c.rect.origin.x.raw())
            << "\" y=\"" << fmt_float(c.rect.origin.y.raw())
            << "\" width=\"" << fmt_float(c.rect.extent.w.raw())
            << "\" height=\"" << fmt_float(c.rect.extent.h.raw())
            << "\"/></clipPath>\n"
            << "<g clip-path=\"url(#clip-" << id << ")\">\n";
    }

    void emit(const ClipPop&) {
        out << "</g>\n";
    }

    void emit_commands(const DrawList& dl) {
        for (const auto& cmd : dl.commands) {
            std::visit([this](const auto& c) { emit(c); }, cmd);
        }
    }
};

} // namespace detail

inline std::string to_svg(const DrawList& dl) {
    auto bb = dl.bounding_box();
    detail::SvgEmitter e;
    e.out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
          << detail::fmt_float(bb.origin.x.raw()) << " "
          << detail::fmt_float(bb.origin.y.raw()) << " "
          << detail::fmt_float(bb.extent.w.raw()) << " "
          << detail::fmt_float(bb.extent.h.raw()) << "\">\n";
    e.emit_commands(dl);
    e.out << "</svg>\n";
    return e.out.str();
}

inline std::string to_svg(const SceneSnapshot& snap) {
    // Compute viewBox from union of all geometry rects
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();

    auto expand = [&](Rect r) {
        min_x = std::min(min_x, r.origin.x.raw());
        min_y = std::min(min_y, r.origin.y.raw());
        max_x = std::max(max_x, r.origin.x.raw() + r.extent.w.raw());
        max_y = std::max(max_y, r.origin.y.raw() + r.extent.h.raw());
    };

    for (const auto& [id, rect] : snap.geometry)
        expand(rect);
    for (const auto& [id, rect] : snap.overlay_geometry)
        expand(rect);

    if (min_x > max_x) { min_x = min_y = 0; max_x = max_y = 0; }

    detail::SvgEmitter e;
    e.out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""
          << detail::fmt_float(min_x) << " "
          << detail::fmt_float(min_y) << " "
          << detail::fmt_float(max_x - min_x) << " "
          << detail::fmt_float(max_y - min_y) << "\">\n";

    for (uint16_t idx : snap.z_order)
        e.emit_commands(snap.draw_lists[idx]);

    if (!snap.overlay.empty())
        e.emit_commands(snap.overlay);

    e.out << "</svg>\n";
    return e.out.str();
}

} // namespace prism
