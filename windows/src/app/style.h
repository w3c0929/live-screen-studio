#pragma once

namespace lz {

struct Rgba {
    float r = 1;
    float g = 1;
    float b = 1;
    float a = 1;
};

enum class InkStyle {
    Marker,
    Neon,
    Highlighter,
    Arrow,
    Dashed,
};

struct InkLook {
    InkStyle style = InkStyle::Marker;
    Rgba color{1.0f, 0.23f, 0.19f, 1.0f};
    double width = 5.0;
};

}  // namespace lz
