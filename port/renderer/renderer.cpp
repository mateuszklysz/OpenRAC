// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "renderer/renderer.h"

#include <chrono>
#include <deque>

#include "renderer/direct.h"
#include "renderer/gl.h"
#include "renderer/subsystems.h"

namespace openrac::renderer {

using namespace openrac::gl;

const char* bucket_name(Bucket bucket) {
    switch (bucket) {
        case Bucket::Sky:
            return "sky";
        case Bucket::Terrain:
            return "terrain";
        case Bucket::Ties:
            return "ties";
        case Bucket::Shrubs:
            return "shrubs";
        case Bucket::Mobys:
            return "mobys";
        case Bucket::Particles:
            return "particles";
        case Bucket::Hud:
            return "hud";
        case Bucket::Count:
            break;
    }
    return "?";
}

// The GPU's side of set_timing: a timer query around each renderer's commands, read once the GPU
// has finished them (a few frames later), so that timing never waits for the GPU.
struct Renderer::Timing {
    struct Pending {
        std::size_t slot;  // in RenderTotals::renderers
        GLuint query;
    };
    std::deque<std::vector<Pending>> frames;  // the frames sent and not yet read, oldest first
    std::vector<GLuint> free;

    GLuint query() {
        if (free.empty()) {
            GLuint q = 0;
            glGenQueries(1, &q);
            return q;
        }
        const GLuint q = free.back();
        free.pop_back();
        return q;
    }

    // Reads the frames the GPU has finished; with `wait`, the oldest also when it has not (so that
    // no more than a few frames are ever waiting).
    void collect(RenderTotals& totals, bool wait) {
        while (!frames.empty()) {
            std::vector<Pending>& oldest = frames.front();
            if (!oldest.empty() && !wait) {
                GLint ready = 0;
                glGetQueryObjectiv(oldest.back().query, GL_QUERY_RESULT_AVAILABLE, &ready);
                if (ready == 0) {
                    return;
                }
            }
            for (const Pending& p : oldest) {
                GLuint64 ns = 0;
                glGetQueryObjectui64v(p.query, GL_QUERY_RESULT, &ns);
                totals.renderers[p.slot].gpu_ms += static_cast<double>(ns) / 1.0e6;
                free.push_back(p.query);
            }
            frames.pop_front();
            wait = false;
        }
    }

    void release() {
        for (const auto& f : frames) {
            for (const Pending& p : f) {
                free.push_back(p.query);
            }
        }
        frames.clear();
        if (!free.empty()) {
            glDeleteQueries(static_cast<GLsizei>(free.size()), free.data());
        }
        free.clear();
    }
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

void Renderer::set_timing(bool on) {
    if (on && !m_timing) {
        m_timing = std::make_unique<Timing>();
    } else if (!on && m_timing) {
        m_timing->release();
        m_timing.reset();
    }
}

bool Renderer::init(std::string& error) {
    RenderState state{m_textures, {}, {}};
    for (auto& r : m_renderers) {
        if (!r->init(state, error)) {
            return false;
        }
    }
    m_initialised = true;
    return true;
}

BucketRenderer* Renderer::add(std::unique_ptr<BucketRenderer> renderer, std::string& error) {
    if (m_initialised) {
        RenderState state{m_textures, {}, {}};
        if (!renderer->init(state, error)) {
            return nullptr;
        }
    }
    m_renderers.push_back(std::move(renderer));
    return m_renderers.back().get();
}

bool Renderer::add_game_renderers(std::string& error) {
    return add(std::make_unique<SkyRenderer>(), error) != nullptr
           && add(std::make_unique<TfragRenderer>(), error) != nullptr
           && add(std::make_unique<TieRenderer>(), error) != nullptr
           && add(std::make_unique<ShrubRenderer>(), error) != nullptr
           && add(std::make_unique<MobyRenderer>(), error) != nullptr
           && add(std::make_unique<ParticleRenderer>(), error) != nullptr
           && add(std::make_unique<DirectRenderer>("hud", Bucket::Hud), error) != nullptr;
}

void Renderer::render(const FrameInput& input, const RenderTarget& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(
        input.clear_colour[0], input.clear_colour[1], input.clear_colour[2], input.clear_colour[3]
    );
    // Reversed depth: nothing is in front of 0, and the tests are GEQUAL.
    glClearDepth(0.0);
    glClear((input.keep_colour ? 0 : GL_COLOR_BUFFER_BIT) | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL);

    RenderState state{m_textures, target, {}};
    // The HUD's renderer is the last of its bucket (the part of the 2D path before the world is
    // drawn by one ahead of the world's).
    std::size_t hud = m_renderers.size();
    for (std::size_t i = 0; i < m_renderers.size(); ++i) {
        if (m_renderers[i]->bucket() == Bucket::Hud) {
            hud = i;
        }
    }
    std::vector<Timing::Pending> pending;
    for (std::size_t i = 0; i < m_renderers.size(); ++i) {
        if (i == hud && input.before_hud) {
            input.before_hud();
            glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
            glViewport(0, 0, target.width, target.height);
        }
        auto& r = m_renderers[i];
        if (!r->enabled) {
            continue;
        }
        if (!m_timing) {
            r->render(input, state);
            continue;
        }
        std::size_t slot = 0;
        while (slot < m_totals.renderers.size() && m_totals.renderers[slot].name != r->name()) {
            ++slot;
        }
        if (slot == m_totals.renderers.size()) {
            m_totals.renderers.push_back({r->name(), 0.0, 0.0});
        }
        const GLuint query = m_timing->query();
        const auto started = std::chrono::steady_clock::now();
        glBeginQuery(GL_TIME_ELAPSED, query);
        r->render(input, state);
        glEndQuery(GL_TIME_ELAPSED);
        m_totals.renderers[slot].cpu_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        pending.push_back({slot, query});
    }
    m_stats = state.stats;
    m_totals.frames++;
    m_totals.draw_calls += static_cast<std::uint64_t>(state.stats.draw_calls);
    m_totals.triangles += state.stats.triangles;
    if (m_timing) {
        m_timing->frames.push_back(std::move(pending));
        // At most four frames in flight: the oldest is read then, waited for if it must be.
        m_timing->collect(m_totals, m_timing->frames.size() > 4);
    }
    m_textures.end_frame();
}

void Renderer::clear_renderers() {
    for (auto& r : m_renderers) {
        r->release();
    }
    m_renderers.clear();
}

void Renderer::release() {
    for (auto& r : m_renderers) {
        r->release();
    }
    if (m_timing) {
        m_timing->release();
    }
    m_textures.release_gl();
}

}  // namespace openrac::renderer
