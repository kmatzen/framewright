#pragma once

namespace framewright {

/// HDR10 static metadata: mastering display primaries/luminance and content
/// light level. Used by VideoWriter to tag output, and populated by
/// VideoReader from the source's side data when present.
struct HDR10Metadata {
    double red_x = 0.708, red_y = 0.292;
    double green_x = 0.170, green_y = 0.797;
    double blue_x = 0.131, blue_y = 0.046;
    double white_x = 0.3127, white_y = 0.3290;
    double max_luminance = 1000.0;
    double min_luminance = 0.0001;
    unsigned int max_cll = 1000;
    unsigned int max_fall = 400;
};

} // namespace framewright
