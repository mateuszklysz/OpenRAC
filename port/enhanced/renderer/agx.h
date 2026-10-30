// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
#pragma once

#include "renderer/framebuffer.h"
#include "renderer/shader.h"

namespace openrac::enhanced {
// Optional SDR colour-treatment experiment, owned and used only by the render worker.
class AgxPreview {
public:
    ~AgxPreview();
    // The image AgX reads, sized for `output`; a pass that draws the whole picture into it lets
    // apply() skip its copy. Null when AgX cannot run.
    renderer::FrameBuffer* source(const renderer::FrameBuffer& output);
    // Tone-maps `output` in place; `source_filled`: source() already holds the picture.
    void apply(renderer::FrameBuffer& output, float exposure, bool source_filled = false);
    void release();

private:
    renderer::FrameBuffer m_copy;
    renderer::Shader m_shader;
    unsigned m_vao = 0;
    bool m_failed = false;
};
}  // namespace openrac::enhanced
