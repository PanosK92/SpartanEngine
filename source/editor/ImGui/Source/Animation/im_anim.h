// im_anim.h — Dear ImGui animation helpers.
// Author: Soufiane KHIAT
// License: MIT
// Version: 1.0.0
//
// - Channels: float, vec2, vec4, int, color (sRGB/Linear/HSV/OKLAB/OKLCH blending).
// - Easing: presets + cubicBezier/steps/back/elastic/bounce/spring.
// - Caching: ImPool + ImGuiStorage keyed by (ImGuiID, channel_id) via ImHashData.
// - Resize helpers: relative targets, resolver callback, explicit rebase.

#pragma once

// Version information
#define IMANIM_VERSION          "1.0.0"
#define IMANIM_VERSION_NUM      10000   // 1.00.00
#define IMANIM_VERSION_MAJOR    1
#define IMANIM_VERSION_MINOR    0
#define IMANIM_VERSION_PATCH    0
#include "../../Source/imgui.h"
#include <stdint.h>
#include <math.h>
#include <limits.h>
#include <float.h>


#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM < 19200 //ImFontBaked and global ImGuiStoragePair were introduced in v19200.
#define IM_ANIM_PRE_19200_COMPATIBILITY
#endif
// PI constants
#ifndef IAM_PI
static float const IAM_PI = 3.1415926535897932384626433832795f;
#endif // !IAM_PI
#ifndef IAM_2PI
static float const IAM_2PI = IAM_PI * 2.0f;
#endif // !IAM_2PI

// ----------------------------------------------------
// Public enums & descriptors (C-style)
// ----------------------------------------------------
enum iam_ease_type {
	iam_ease_linear = 0,
	iam_ease_in_quad,  iam_ease_out_quad,  iam_ease_in_out_quad,
	iam_ease_in_cubic, iam_ease_out_cubic, iam_ease_in_out_cubic,
	iam_ease_in_quart, iam_ease_out_quart, iam_ease_in_out_quart,
	iam_ease_in_quint, iam_ease_out_quint, iam_ease_in_out_quint,
	iam_ease_in_sine,  iam_ease_out_sine,  iam_ease_in_out_sine,
	iam_ease_in_expo,  iam_ease_out_expo,  iam_ease_in_out_expo,
	iam_ease_in_circ,  iam_ease_out_circ,  iam_ease_in_out_circ,
	iam_ease_in_back,  iam_ease_out_back,  iam_ease_in_out_back,        // p0 = overshoot
	iam_ease_in_elastic, iam_ease_out_elastic, iam_ease_in_out_elastic,  // p0 = amplitude, p1 = period
	iam_ease_in_bounce,  iam_ease_out_bounce,  iam_ease_in_out_bounce,
	iam_ease_steps,         // p0 = steps (>=1), p1 = 0:end 1:start 2:both
	iam_ease_cubic_bezier,  // p0=x1 p1=y1 p2=x2 p3=y2
	iam_ease_spring,        // p0=mass p1=stiffness p2=damping p3=v0
	iam_ease_custom         // User-defined easing function (use iam_ease_custom_fn)
};

enum iam_policy {
	iam_policy_crossfade = 0,	// smooth into new target
	iam_policy_cut,				// snap to target
	iam_policy_queue			// queue one pending target
};

enum iam_color_space {
	iam_col_srgb = 0,			// blend in sRGB (not physically linear)
	iam_col_srgb_linear,		// sRGB<->linear, blend in linear, back to sRGB
	iam_col_hsv,				// blend H/S/V (hue shortest arc), keep A linear
	iam_col_oklab,				// sRGB<->OKLAB, blend in OKLAB, back to sRGB
	iam_col_oklch				// sRGB<->OKLCH (cylindrical OKLAB), blend in OKLCH, back to sRGB
};

enum iam_anchor_space {
	iam_anchor_window_content = 0,	// ImGui::GetContentRegionAvail()
	iam_anchor_window,				// ImGui::GetWindowSize()
	iam_anchor_viewport,			// ImGui::GetWindowViewport()->Size
	iam_anchor_last_item			// ImGui::GetItemRectSize()
};

// Descriptor for any easing (preset or parametric)
struct iam_ease_desc {
	int		type;	// iam_ease_type
	float	p0, p1, p2, p3;
};

// Custom easing function callback (t in [0,1], returns eased value)
typedef float (*iam_ease_fn)(float t);

// ----------------------------------------------------
// Public API declarations
// ----------------------------------------------------

// Frame management
void iam_update_begin_frame();                                                      // Call once per frame before any tweens.
void iam_gc(unsigned int max_age_frames = 600);                                     // Remove stale tween entries older than max_age_frames.
void iam_pool_clear();																	// Manually clean up pools.
void iam_reserve(int cap_float, int cap_vec2, int cap_vec4, int cap_int, int cap_color); // Pre-allocate pool capacity.
void iam_set_ease_lut_samples(int count);                                           // Set LUT resolution for parametric easings (default: 256).

// Global time scale (for slow-motion / fast-forward debugging)
void  iam_set_global_time_scale(float scale);                                       // Set global time multiplier (1.0 = normal, 0.5 = half speed, 2.0 = double).
float iam_get_global_time_scale();                                                  // Get current global time scale.

// Lazy Initialization - defer channel creation until animation is needed
void iam_set_lazy_init(bool enable);                                                // Enable/disable lazy initialization (default: true).
bool iam_is_lazy_init_enabled();                                                    // Check if lazy init is enabled.

// Custom easing functions
void iam_register_custom_ease(int slot, iam_ease_fn fn);                            // Register custom easing in slot 0-15. Use with iam_ease_custom_fn(slot).
iam_ease_fn iam_get_custom_ease(int slot);                                          // Get registered custom easing function.

// Debug UI
void iam_show_unified_inspector(bool* p_open = nullptr);                            // Show unified inspector (merges debug window + animation inspector).
void iam_show_debug_timeline(ImGuiID instance_id);                                  // Show debug timeline for a clip instance.

// Performance Profiler
void iam_profiler_enable(bool enable);                                              // Enable/disable the performance profiler.
bool iam_profiler_is_enabled();                                                     // Check if profiler is enabled.
void iam_profiler_begin_frame();                                                    // Call at frame start when profiler is enabled.
void iam_profiler_end_frame();                                                      // Call at frame end when profiler is enabled.
void iam_profiler_begin(const char* name);                                          // Begin a named profiler section.
void iam_profiler_end();                                                            // End the current profiler section.

// Drag Feedback - animated feedback for drag operations
struct iam_drag_opts {
	ImVec2 snap_grid;              // Grid size for snapping (0,0 = no grid)
	ImVec2* snap_points;           // Array of custom snap points
	int snap_points_count;         // Number of snap points
	float snap_duration;           // Duration of snap animation
	float overshoot;               // Overshoot amount (0 = none, 1 = normal)
	int ease_type;                 // Easing type for snap animation

	iam_drag_opts() : snap_grid(0, 0), snap_points(nullptr), snap_points_count(0),
		snap_duration(0.2f), overshoot(0), ease_type(iam_ease_out_cubic) {}
};

struct iam_drag_feedback {
	ImVec2 position;               // Current animated position
	ImVec2 offset;                 // Offset from drag start
	ImVec2 velocity;               // Current velocity estimate
	bool is_dragging;              // Currently being dragged
	bool is_snapping;              // Currently snapping to target
	float snap_progress;           // Snap animation progress (0-1)
};

iam_drag_feedback iam_drag_begin(ImGuiID id, ImVec2 pos);                           // Start tracking drag at position.
iam_drag_feedback iam_drag_update(ImGuiID id, ImVec2 pos, float dt);                // Update drag position during drag.
iam_drag_feedback iam_drag_release(ImGuiID id, ImVec2 pos, iam_drag_opts const& opts, float dt); // Release drag with animated feedback.
void iam_drag_cancel(ImGuiID id);                                                   // Cancel drag tracking.

// Oscillators - continuous periodic animations
enum iam_wave_type {
	iam_wave_sine = 0,      // Smooth sine wave
	iam_wave_triangle,      // Triangle wave (linear up/down)
	iam_wave_sawtooth,      // Sawtooth wave (linear up, instant reset)
	iam_wave_square         // Square wave (on/off pulse)
};
float  iam_oscillate(ImGuiID id, float amplitude, float frequency, int wave_type, float phase, float dt);       // Returns oscillating value [-amplitude, +amplitude].
int    iam_oscillate_int(ImGuiID id, int amplitude, float frequency, int wave_type, float phase, float dt);      // Returns oscillating integer value [-amplitude, +amplitude].
ImVec2 iam_oscillate_vec2(ImGuiID id, ImVec2 amplitude, ImVec2 frequency, int wave_type, ImVec2 phase, float dt); // 2D oscillation.
ImVec4 iam_oscillate_vec4(ImGuiID id, ImVec4 amplitude, ImVec4 frequency, int wave_type, ImVec4 phase, float dt); // 4D oscillation.
ImVec4 iam_oscillate_color(ImGuiID id, ImVec4 base_color, ImVec4 amplitude, float frequency, int wave_type, float phase, int color_space, float dt); // Color oscillation in specified color space.

// Shake/Wiggle - procedural noise animations
float  iam_shake(ImGuiID id, float intensity, float frequency, float decay_time, float dt);       // Decaying random shake. Returns offset that decays to 0.
int    iam_shake_int(ImGuiID id, int intensity, float frequency, float decay_time, float dt);     // Decaying random shake for integers.
ImVec2 iam_shake_vec2(ImGuiID id, ImVec2 intensity, float frequency, float decay_time, float dt); // 2D decaying shake.
ImVec4 iam_shake_vec4(ImGuiID id, ImVec4 intensity, float frequency, float decay_time, float dt); // 4D decaying shake.
ImVec4 iam_shake_color(ImGuiID id, ImVec4 base_color, ImVec4 intensity, float frequency, float decay_time, int color_space, float dt); // Color shake in specified color space.
float  iam_wiggle(ImGuiID id, float amplitude, float frequency, float dt);                        // Continuous smooth random movement.
int    iam_wiggle_int(ImGuiID id, int amplitude, float frequency, float dt);                      // Continuous smooth random movement for integers.
ImVec2 iam_wiggle_vec2(ImGuiID id, ImVec2 amplitude, float frequency, float dt);                  // 2D continuous wiggle.
ImVec4 iam_wiggle_vec4(ImGuiID id, ImVec4 amplitude, float frequency, float dt);                  // 4D continuous wiggle.
ImVec4 iam_wiggle_color(ImGuiID id, ImVec4 base_color, ImVec4 amplitude, float frequency, int color_space, float dt); // Color wiggle in specified color space.
void   iam_trigger_shake(ImGuiID id);                                                             // Trigger/restart a shake animation.

// Easing evaluation
float iam_eval_preset(int type, float t);                                           // Evaluate a preset easing function at time t (0-1).

// Tween API - smoothly interpolate values over time
// init_value: Initial value when channel is first created. Defaults to 0 (or white for color).
//             Use this to avoid unwanted animations when the first target differs from the default.
float  iam_tween_float(ImGuiID id, ImGuiID channel_id, float target, float dur, iam_ease_desc const& ez, int policy, float dt, float init_value = 0.0f);   // Animate a float value.
ImVec2 iam_tween_vec2(ImGuiID id, ImGuiID channel_id, ImVec2 target, float dur, iam_ease_desc const& ez, int policy, float dt, ImVec2 init_value = ImVec2(0, 0));   // Animate a 2D vector.
ImVec4 iam_tween_vec4(ImGuiID id, ImGuiID channel_id, ImVec4 target, float dur, iam_ease_desc const& ez, int policy, float dt, ImVec4 init_value = ImVec4(0, 0, 0, 0));   // Animate a 4D vector.
int    iam_tween_int(ImGuiID id, ImGuiID channel_id, int target, float dur, iam_ease_desc const& ez, int policy, float dt, int init_value = 0);       // Animate an integer value.
ImVec4 iam_tween_color(ImGuiID id, ImGuiID channel_id, ImVec4 target_srgb, float dur, iam_ease_desc const& ez, int policy, int color_space, float dt, ImVec4 init_value = ImVec4(1, 1, 1, 1)); // Animate a color in specified color space.

// Resize-friendly helpers
ImVec2 iam_anchor_size(int space); // Get dimensions of anchor space (window, viewport, etc.).

// Relative target tweens (percent of anchor + pixel offset) - survive window resizes
float  iam_tween_float_rel(ImGuiID id, ImGuiID channel_id, float percent, float px_bias, float dur, iam_ease_desc const& ez, int policy, int anchor_space, int axis, float dt);  // Float relative to anchor (axis: 0=x, 1=y).
ImVec2 iam_tween_vec2_rel(ImGuiID id, ImGuiID channel_id, ImVec2 percent, ImVec2 px_bias, float dur, iam_ease_desc const& ez, int policy, int anchor_space, float dt);           // Vec2 relative to anchor.
ImVec4 iam_tween_vec4_rel(ImGuiID id, ImGuiID channel_id, ImVec4 percent, ImVec4 px_bias, float dur, iam_ease_desc const& ez, int policy, int anchor_space, float dt);           // Vec4 with x,y relative to anchor.
ImVec4 iam_tween_color_rel(ImGuiID id, ImGuiID channel_id, ImVec4 percent, ImVec4 px_bias, float dur, iam_ease_desc const& ez, int policy, int color_space, int anchor_space, float dt); // Color with component offsets.

// Resolver callbacks for dynamic target computation
typedef float  (*iam_float_resolver)(void* user);   // Returns float target value.
typedef ImVec2 (*iam_vec2_resolver)(void* user);    // Returns vec2 target value.
typedef ImVec4 (*iam_vec4_resolver)(void* user);    // Returns vec4 target value.
typedef ImVec4 (*iam_color_resolver)(void* user);   // Returns color target (sRGB).
typedef int    (*iam_int_resolver)(void* user);     // Returns int target value.

// Resolved tweens - target computed dynamically by callback each frame
float  iam_tween_float_resolved(ImGuiID id, ImGuiID channel_id, iam_float_resolver fn, void* user, float dur, iam_ease_desc const& ez, int policy, float dt);                     // Float with dynamic target.
ImVec2 iam_tween_vec2_resolved(ImGuiID id, ImGuiID channel_id, iam_vec2_resolver fn, void* user, float dur, iam_ease_desc const& ez, int policy, float dt);                       // Vec2 with dynamic target.
ImVec4 iam_tween_vec4_resolved(ImGuiID id, ImGuiID channel_id, iam_vec4_resolver fn, void* user, float dur, iam_ease_desc const& ez, int policy, float dt);                       // Vec4 with dynamic target.
ImVec4 iam_tween_color_resolved(ImGuiID id, ImGuiID channel_id, iam_color_resolver fn, void* user, float dur, iam_ease_desc const& ez, int policy, int color_space, float dt);    // Color with dynamic target.
int    iam_tween_int_resolved(ImGuiID id, ImGuiID channel_id, iam_int_resolver fn, void* user, float dur, iam_ease_desc const& ez, int policy, float dt);                         // Int with dynamic target.

// Rebase functions - change target of in-progress animation without restarting
void iam_rebase_float(ImGuiID id, ImGuiID channel_id, float new_target, float dt);  // Smoothly redirect float animation to new target.
void iam_rebase_vec2(ImGuiID id, ImGuiID channel_id, ImVec2 new_target, float dt);  // Smoothly redirect vec2 animation to new target.
void iam_rebase_vec4(ImGuiID id, ImGuiID channel_id, ImVec4 new_target, float dt);  // Smoothly redirect vec4 animation to new target.
void iam_rebase_color(ImGuiID id, ImGuiID channel_id, ImVec4 new_target, float dt); // Smoothly redirect color animation to new target.
void iam_rebase_int(ImGuiID id, ImGuiID channel_id, int new_target, float dt);      // Smoothly redirect int animation to new target.

// Color blending utility
ImVec4 iam_get_blended_color(ImVec4 a_srgb, ImVec4 b_srgb, float t, int color_space);  // Blend two sRGB colors in specified color space.

// ----------------------------------------------------
// Convenience shorthands for common easings
// ----------------------------------------------------
inline iam_ease_desc iam_ease_preset(int type) { iam_ease_desc e = { type, 0,0,0,0 }; return e; }                                               // Create descriptor from preset enum.
inline iam_ease_desc iam_ease_bezier(float x1, float y1, float x2, float y2) { iam_ease_desc e = { iam_ease_cubic_bezier, x1,y1,x2,y2 }; return e; } // Create cubic bezier easing.
inline iam_ease_desc iam_ease_steps_desc(int steps, int mode) { iam_ease_desc e = { iam_ease_steps, (float)steps,(float)mode,0,0 }; return e; }     // Create step function easing.
inline iam_ease_desc iam_ease_back(float overshoot) { iam_ease_desc e = { iam_ease_out_back, overshoot,0,0,0 }; return e; }                         // Create back easing with overshoot.
inline iam_ease_desc iam_ease_elastic(float amplitude, float period) { iam_ease_desc e = { iam_ease_out_elastic, amplitude, period,0,0 }; return e; } // Create elastic easing.
inline iam_ease_desc iam_ease_spring_desc(float mass, float stiffness, float damping, float v0) { iam_ease_desc e = { iam_ease_spring, mass, stiffness, damping, v0 }; return e; } // Create physics spring.
inline iam_ease_desc iam_ease_custom_fn(int slot) { iam_ease_desc e = { iam_ease_custom, (float)slot,0,0,0 }; return e; }                                                         // Use registered custom easing (slot 0-15).

// Scroll animation - smooth scrolling for ImGui windows
void iam_scroll_to_y(float target_y, float duration, iam_ease_desc const& ez = iam_ease_preset(iam_ease_out_cubic));           // Scroll current window to Y position.
void iam_scroll_to_x(float target_x, float duration, iam_ease_desc const& ez = iam_ease_preset(iam_ease_out_cubic));           // Scroll current window to X position.
void iam_scroll_to_top(float duration = 0.3f, iam_ease_desc const& ez = iam_ease_preset(iam_ease_out_cubic));                  // Scroll to top of window.
void iam_scroll_to_bottom(float duration = 0.3f, iam_ease_desc const& ez = iam_ease_preset(iam_ease_out_cubic));               // Scroll to bottom of window.

// ----------------------------------------------------
// Per-axis easing - different easing per component
// ----------------------------------------------------

// Per-axis easing descriptor (for vec2/vec4/color)
struct iam_ease_per_axis {
	iam_ease_desc x;             // Easing for X component
	iam_ease_desc y;             // Easing for Y component
	iam_ease_desc z;             // Easing for Z component (vec4/color only)
	iam_ease_desc w;             // Easing for W/alpha component (vec4/color only)

	iam_ease_per_axis()
		: x(iam_ease_preset(iam_ease_linear)), y(iam_ease_preset(iam_ease_linear)),
		  z(iam_ease_preset(iam_ease_linear)), w(iam_ease_preset(iam_ease_linear)) {}

	iam_ease_per_axis(iam_ease_desc all)
		: x(all), y(all), z(all), w(all) {}

	iam_ease_per_axis(iam_ease_desc ex, iam_ease_desc ey)
		: x(ex), y(ey), z(iam_ease_preset(iam_ease_linear)), w(iam_ease_preset(iam_ease_linear)) {}

	iam_ease_per_axis(iam_ease_desc ex, iam_ease_desc ey, iam_ease_desc ez, iam_ease_desc ew)
		: x(ex), y(ey), z(ez), w(ew) {}
};

// Tween with per-axis easing - each component uses its own easing curve
ImVec2 iam_tween_vec2_per_axis(ImGuiID id, ImGuiID channel_id, ImVec2 target, float dur, iam_ease_per_axis const& ez, int policy, float dt);
ImVec4 iam_tween_vec4_per_axis(ImGuiID id, ImGuiID channel_id, ImVec4 target, float dur, iam_ease_per_axis const& ez, int policy, float dt);
ImVec4 iam_tween_color_per_axis(ImGuiID id, ImGuiID channel_id, ImVec4 target_srgb, float dur, iam_ease_per_axis const& ez, int policy, int color_space, float dt);

// ----------------------------------------------------
// Motion Paths - animate along curves and splines
// ----------------------------------------------------

// Path segment types
enum iam_path_segment_type {
	iam_seg_line = 0,            // Linear segment to endpoint
	iam_seg_quadratic_bezier,    // Quadratic bezier (1 control point)
	iam_seg_cubic_bezier,        // Cubic bezier (2 control points)
	iam_seg_catmull_rom          // Catmull-rom spline segment
};

// Single-curve evaluation functions (stateless, for direct use)
ImVec2 iam_bezier_quadratic(ImVec2 p0, ImVec2 p1, ImVec2 p2, float t);                              // Evaluate quadratic bezier at t [0,1].
ImVec2 iam_bezier_cubic(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t);                       // Evaluate cubic bezier at t [0,1].
ImVec2 iam_catmull_rom(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t, float tension = 0.5f);  // Evaluate Catmull-Rom spline at t [0,1]. Points go through p1 and p2.

// Derivatives (for tangent/velocity)
ImVec2 iam_bezier_quadratic_deriv(ImVec2 p0, ImVec2 p1, ImVec2 p2, float t);                        // Derivative of quadratic bezier.
ImVec2 iam_bezier_cubic_deriv(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t);                 // Derivative of cubic bezier.
ImVec2 iam_catmull_rom_deriv(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t, float tension = 0.5f); // Derivative of Catmull-Rom.

// Forward declaration
struct iam_path_data;

// iam_path - fluent API for building multi-segment motion paths
class iam_path {
public:
	static iam_path begin(ImGuiID path_id, ImVec2 start);                                           // Start building a path at position.

	iam_path& line_to(ImVec2 end);                                                                  // Add linear segment to endpoint.
	iam_path& quadratic_to(ImVec2 ctrl, ImVec2 end);                                                // Add quadratic bezier segment.
	iam_path& cubic_to(ImVec2 ctrl1, ImVec2 ctrl2, ImVec2 end);                                     // Add cubic bezier segment.
	iam_path& catmull_to(ImVec2 end, float tension = 0.5f);                                         // Add Catmull-Rom segment to endpoint.
	iam_path& close();                                                                              // Close path back to start point.

	void end();                                                                                     // Finalize and register path.

	ImGuiID id() const { return m_path_id; }

private:
	iam_path(ImGuiID path_id) : m_path_id(path_id) {}
	ImGuiID m_path_id;
};

// Query path info
bool   iam_path_exists(ImGuiID path_id);                                                            // Check if path exists.
float  iam_path_length(ImGuiID path_id);                                                            // Get approximate path length.
ImVec2 iam_path_evaluate(ImGuiID path_id, float t);                                                 // Sample path at t [0,1].
ImVec2 iam_path_tangent(ImGuiID path_id, float t);                                                  // Get tangent (normalized direction) at t.
float  iam_path_angle(ImGuiID path_id, float t);                                                    // Get rotation angle (radians) at t.

// Tween along a path
ImVec2 iam_tween_path(ImGuiID id, ImGuiID channel_id, ImGuiID path_id, float dur, iam_ease_desc const& ez, int policy, float dt);   // Animate position along path.
float  iam_tween_path_angle(ImGuiID id, ImGuiID channel_id, ImGuiID path_id, float dur, iam_ease_desc const& ez, int policy, float dt); // Animate rotation angle along path.

// ----------------------------------------------------
// Arc-length parameterization (for constant-speed animation)
// ----------------------------------------------------

// Build arc-length lookup table for a path (call once per path, improves accuracy)
void   iam_path_build_arc_lut(ImGuiID path_id, int subdivisions = 64);                              // Build LUT with specified resolution.
bool   iam_path_has_arc_lut(ImGuiID path_id);                                                       // Check if path has precomputed LUT.

// Distance-based path evaluation (uses arc-length LUT for constant speed)
float  iam_path_distance_to_t(ImGuiID path_id, float distance);                                     // Convert arc-length distance to t parameter.
ImVec2 iam_path_evaluate_at_distance(ImGuiID path_id, float distance);                              // Get position at arc-length distance.
float  iam_path_angle_at_distance(ImGuiID path_id, float distance);                                 // Get rotation angle at arc-length distance.
ImVec2 iam_path_tangent_at_distance(ImGuiID path_id, float distance);                               // Get tangent at arc-length distance.

// ----------------------------------------------------
// Path Morphing - interpolate between two paths
// ----------------------------------------------------

// Morph options for path interpolation
struct iam_morph_opts {
	int   samples;               // Number of sample points for resampling (default: 64)
	bool  match_endpoints;       // Force endpoints to match exactly (default: true)
	bool  use_arc_length;        // Use arc-length parameterization for smoother morphing (default: true)

	iam_morph_opts() : samples(64), match_endpoints(true), use_arc_length(true) {}
};

// Evaluate morphed path at parameter t [0,1] with blend factor [0,1]
// path_a at blend=0, path_b at blend=1
// Paths can have different numbers of segments - they are resampled to match
ImVec2 iam_path_morph(ImGuiID path_a, ImGuiID path_b, float t, float blend, iam_morph_opts const& opts = iam_morph_opts());

// Get tangent of morphed path
ImVec2 iam_path_morph_tangent(ImGuiID path_a, ImGuiID path_b, float t, float blend, iam_morph_opts const& opts = iam_morph_opts());

// Get angle (radians) of morphed path
float  iam_path_morph_angle(ImGuiID path_a, ImGuiID path_b, float t, float blend, iam_morph_opts const& opts = iam_morph_opts());

// Tween along a morphing path - animates both position along path AND the morph blend
ImVec2 iam_tween_path_morph(ImGuiID id, ImGuiID channel_id, ImGuiID path_a, ImGuiID path_b,
                            float target_blend, float dur, iam_ease_desc const& path_ease,
                            iam_ease_desc const& morph_ease, int policy, float dt,
                            iam_morph_opts const& opts = iam_morph_opts());

// Get current morph blend value from a tween (for querying state)
float  iam_get_morph_blend(ImGuiID id, ImGuiID channel_id);

// ----------------------------------------------------
// Text along motion paths
// ----------------------------------------------------

// Text alignment along path
enum iam_text_path_align {
	iam_text_align_start = 0,    // Text starts at path start (or offset)
	iam_text_align_center,       // Text centered on path
	iam_text_align_end           // Text ends at path end
};

// Text path options
struct iam_text_path_opts {
	ImVec2 origin;               // Screen-space origin for rendering (path coords are offset by this)
	float offset;                // Starting offset along path (pixels)
	float letter_spacing;        // Extra spacing between characters (pixels)
	int   align;                 // iam_text_path_align value
	bool  flip_y;                // Flip text vertically (for paths going right-to-left)
	ImU32 color;                 // Text color (default: white)
	ImFont* font;                // Font to use (nullptr = current font)
	float font_scale;            // Additional font scale (1.0 = normal)

	iam_text_path_opts() : origin(0, 0), offset(0), letter_spacing(0), align(iam_text_align_start),
	                       flip_y(false), color(IM_COL32_WHITE), font(nullptr), font_scale(1.0f) {}
};

// Render text along a path (static - no animation)
void iam_text_path(ImGuiID path_id, const char* text, iam_text_path_opts const& opts = iam_text_path_opts());

// Animated text along path (characters appear progressively)
void iam_text_path_animated(ImGuiID path_id, const char* text, float progress, iam_text_path_opts const& opts = iam_text_path_opts());

// Helper: Get text width for path layout calculations
float iam_text_path_width(const char* text, iam_text_path_opts const& opts = iam_text_path_opts());

// ----------------------------------------------------
// Quad transform helpers (for advanced custom rendering)
// ----------------------------------------------------

// Transform a quad (4 vertices) by rotation and translation
void iam_transform_quad(ImVec2* quad, ImVec2 center, float angle_rad, ImVec2 translation);

// Create a rotated quad for a glyph at a position on the path
void iam_make_glyph_quad(ImVec2* quad, ImVec2 pos, float angle_rad, float glyph_width, float glyph_height, float baseline_offset = 0.0f);

// ----------------------------------------------------
// Text Stagger - per-character animation effects
// ----------------------------------------------------

// Text stagger effect types
enum iam_text_stagger_effect {
	iam_text_fx_none = 0,        // No effect (instant appear)
	iam_text_fx_fade,            // Fade in alpha
	iam_text_fx_scale,           // Scale from center
	iam_text_fx_slide_up,        // Slide up from below
	iam_text_fx_slide_down,      // Slide down from above
	iam_text_fx_slide_left,      // Slide in from right
	iam_text_fx_slide_right,     // Slide in from left
	iam_text_fx_rotate,          // Rotate in
	iam_text_fx_bounce,          // Bounce in with overshoot
	iam_text_fx_wave,            // Wave motion (continuous)
	iam_text_fx_typewriter       // Typewriter style (instant per char)
};

// Text stagger options
struct iam_text_stagger_opts {
	ImVec2 pos;                  // Base position for text
	int    effect;               // iam_text_stagger_effect
	float  char_delay;           // Delay between each character (seconds)
	float  char_duration;        // Duration of each character's animation (seconds)
	float  effect_intensity;     // Intensity of effect (pixels for slide, degrees for rotate, scale factor)
	iam_ease_desc ease;          // Easing for character animation
	ImU32  color;                // Text color
	ImFont* font;                // Font to use (nullptr = current)
	float  font_scale;           // Font scale multiplier
	float  letter_spacing;       // Extra spacing between characters

	iam_text_stagger_opts()
		: pos(0, 0), effect(iam_text_fx_fade), char_delay(0.05f), char_duration(0.3f),
		  effect_intensity(20.0f), ease(iam_ease_preset(iam_ease_out_cubic)),
		  color(IM_COL32_WHITE), font(nullptr), font_scale(1.0f), letter_spacing(0.0f) {}
};

// Render text with per-character stagger animation
void iam_text_stagger(ImGuiID id, const char* text, float progress, iam_text_stagger_opts const& opts = iam_text_stagger_opts());

// Get text width for layout calculations
float iam_text_stagger_width(const char* text, iam_text_stagger_opts const& opts = iam_text_stagger_opts());

// Get total animation duration for text (accounts for stagger delays)
float iam_text_stagger_duration(const char* text, iam_text_stagger_opts const& opts = iam_text_stagger_opts());

// ----------------------------------------------------
// Noise Channels - Perlin/Simplex noise for organic movement
// ----------------------------------------------------

// Noise types
enum iam_noise_type {
	iam_noise_perlin = 0,        // Classic Perlin noise
	iam_noise_simplex,           // Simplex noise (faster, fewer artifacts)
	iam_noise_value,             // Value noise (blocky)
	iam_noise_worley             // Worley/cellular noise
};

// Noise options
struct iam_noise_opts {
	int   type;                  // iam_noise_type
	int   octaves;               // Number of octaves for fractal noise (1-8)
	float persistence;           // Amplitude multiplier per octave (0.0-1.0)
	float lacunarity;            // Frequency multiplier per octave (typically 2.0)
	int   seed;                  // Random seed for noise generation

	iam_noise_opts()
		: type(iam_noise_perlin), octaves(4), persistence(0.5f), lacunarity(2.0f), seed(0) {}
};

// Sample noise at a point (returns value in [-1, 1])
float  iam_noise_2d(float x, float y, iam_noise_opts const& opts = iam_noise_opts());                    // 2D noise
float  iam_noise_3d(float x, float y, float z, iam_noise_opts const& opts = iam_noise_opts());        // 3D noise

// Animated noise channels - continuous noise that evolves over time
float  iam_noise_channel_float(ImGuiID id, float frequency, float amplitude, iam_noise_opts const& opts, float dt);  // 1D animated noise
ImVec2 iam_noise_channel_vec2(ImGuiID id, ImVec2 frequency, ImVec2 amplitude, iam_noise_opts const& opts, float dt); // 2D animated noise
ImVec4 iam_noise_channel_vec4(ImGuiID id, ImVec4 frequency, ImVec4 amplitude, iam_noise_opts const& opts, float dt); // 4D animated noise
ImVec4 iam_noise_channel_color(ImGuiID id, ImVec4 base_color, ImVec4 amplitude, float frequency, iam_noise_opts const& opts, int color_space, float dt); // Animated color noise in specified color space

// Convenience: smooth random movement (like wiggle but using noise)
float  iam_smooth_noise_float(ImGuiID id, float amplitude, float speed, float dt);                          // Simple 1D smooth noise
ImVec2 iam_smooth_noise_vec2(ImGuiID id, ImVec2 amplitude, float speed, float dt);                    // Simple 2D smooth noise
ImVec4 iam_smooth_noise_vec4(ImGuiID id, ImVec4 amplitude, float speed, float dt);                    // Simple 4D smooth noise
ImVec4 iam_smooth_noise_color(ImGuiID id, ImVec4 base_color, ImVec4 amplitude, float speed, int color_space, float dt); // Smooth noise for colors in specified color space

// ----------------------------------------------------
// Style Interpolation - animate between ImGuiStyle themes
// ----------------------------------------------------

// Register a named style for interpolation
void iam_style_register(ImGuiID style_id, ImGuiStyle const& style);                                   // Register a style snapshot
void iam_style_register_current(ImGuiID style_id);                                                    // Register current ImGui style

// Blend between two registered styles (result applied to ImGui::GetStyle())
// Uses iam_color_space for color blending mode (iam_col_oklab recommended)
void iam_style_blend(ImGuiID style_a, ImGuiID style_b, float t, int color_space = iam_col_oklab);

// Tween between styles over time
void iam_style_tween(ImGuiID id, ImGuiID target_style, float duration, iam_ease_desc const& ease, int color_space, float dt);

// Get interpolated style without applying
void iam_style_blend_to(ImGuiID style_a, ImGuiID style_b, float t, ImGuiStyle* out_style, int color_space = iam_col_oklab);

// Check if a style is registered
bool iam_style_exists(ImGuiID style_id);

// Remove a registered style
void iam_style_unregister(ImGuiID style_id);

// ----------------------------------------------------
// Gradient Interpolation - animate between color gradients
// ----------------------------------------------------

// Color gradient with any number of stops (sorted by position)
struct iam_gradient {
	ImVector<float> positions;   // Positions along gradient [0,1], kept sorted
	ImVector<ImVec4> colors;     // Colors at each position (sRGB)

	iam_gradient() {}

	// Add a stop to the gradient (automatically sorted by position)
	iam_gradient& add(float position, ImVec4 color);
	iam_gradient& add(float position, ImU32 color) {
		return add(position, ImGui::ColorConvertU32ToFloat4(color));
	}

	// Get stop count
	int stop_count() const { return positions.Size; }

	// Sample the gradient at position t [0,1]
	ImVec4 sample(float t, int color_space = iam_col_oklab) const;

	// Create common gradients
	static iam_gradient solid(ImVec4 color);
	static iam_gradient two_color(ImVec4 start, ImVec4 end);
	static iam_gradient three_color(ImVec4 start, ImVec4 mid, ImVec4 end);
};

// Blend between two gradients
iam_gradient iam_gradient_lerp(iam_gradient const& a, iam_gradient const& b, float t, int color_space = iam_col_oklab);

// Tween between gradients over time
iam_gradient iam_tween_gradient(ImGuiID id, ImGuiID channel_id, iam_gradient const& target, float dur, iam_ease_desc const& ez, int policy, int color_space, float dt);

// ----------------------------------------------------
// Transform Interpolation - animate 2D transforms
// ----------------------------------------------------

// Rotation interpolation modes
enum iam_rotation_mode {
	iam_rotation_shortest = 0,   // Shortest path (default) - never rotates more than 180 degrees
	iam_rotation_longest,        // Longest path - always takes the long way around
	iam_rotation_cw,             // Clockwise - always rotates clockwise (positive direction)
	iam_rotation_ccw,            // Counter-clockwise - always rotates counter-clockwise
	iam_rotation_direct          // Direct lerp - no angle unwrapping, can cause spinning for large deltas
};

// 2D transform (position, rotation, scale)
struct iam_transform {
	ImVec2 position;             // Translation
	ImVec2 scale;                // Scale (1,1 = identity)
	float rotation;              // Rotation in radians

	iam_transform() : position(0, 0), scale(1, 1), rotation(0) {}
	iam_transform(ImVec2 pos, float rot = 0, ImVec2 scl = ImVec2(1, 1))
		: position(pos), scale(scl), rotation(rot) {}

	// Create identity transform
	static iam_transform identity() { return iam_transform(); }

	// Combine transforms (this * other)
	iam_transform operator*(iam_transform const& other) const;

	// Apply transform to a point
	ImVec2 apply(ImVec2 point) const;

	// Get inverse transform
	iam_transform inverse() const;
};

// Blend between two transforms with rotation interpolation
iam_transform iam_transform_lerp(iam_transform const& a, iam_transform const& b, float t, int rotation_mode = iam_rotation_shortest);

// Tween between transforms over time
iam_transform iam_tween_transform(ImGuiID id, ImGuiID channel_id, iam_transform const& target, float dur, iam_ease_desc const& ez, int policy, int rotation_mode, float dt);

// Decompose a 3x2 matrix into transform components
iam_transform iam_transform_from_matrix(float m00, float m01, float m10, float m11, float tx, float ty);

// Convert transform to 3x2 matrix (row-major: [m00 m01 tx; m10 m11 ty])
void iam_transform_to_matrix(iam_transform const& t, float* out_matrix);

// ============================================================
// CLIP-BASED ANIMATION SYSTEM
// ============================================================

// Direction for looping animations
enum iam_direction {
	iam_dir_normal = 0,		// play forward
	iam_dir_reverse,		// play backward
	iam_dir_alternate		// ping-pong
};

// Channel types for keyframes
enum iam_channel_type {
	iam_chan_float = 0,
	iam_chan_vec2,
	iam_chan_vec4,
	iam_chan_int,
	iam_chan_color,     // Color with color space (stores in vec4 + color_space metadata)
	iam_chan_float_rel, // Float relative to anchor (percent + px_bias)
	iam_chan_vec2_rel,  // Vec2 relative to anchor (percent.xy + px_bias.xy)
	iam_chan_vec4_rel,  // Vec4 relative to anchor (percent.xy + px_bias.xy for x,y; z,w absolute)
	iam_chan_color_rel  // Color relative to anchor (for position-based color effects)
};

// Result codes
enum iam_result {
	iam_ok = 0,
	iam_err_not_found,
	iam_err_bad_arg,
	iam_err_no_mem
};

// Spring parameters for physics-based animation
struct iam_spring_params {
	float mass;
	float stiffness;
	float damping;
	float initial_velocity;
};

// ----------------------------------------------------
// Repeat with Variation - per-loop parameter changes
// ----------------------------------------------------

// Variation modes for repeat animations
enum iam_variation_mode {
	iam_var_none = 0,       // No variation
	iam_var_increment,      // Add amount each iteration
	iam_var_decrement,      // Subtract amount each iteration
	iam_var_multiply,       // Multiply by factor each iteration
	iam_var_random,         // Random in range [-amount, +amount]
	iam_var_random_abs,     // Random in range [0, amount]
	iam_var_pingpong,       // Alternate +/- each iteration
	iam_var_callback        // Use custom callback
};

// Callback types for custom variation logic
typedef float  (*iam_variation_float_fn)(int index, void* user);
typedef int    (*iam_variation_int_fn)(int index, void* user);
typedef ImVec2 (*iam_variation_vec2_fn)(int index, void* user);
typedef ImVec4 (*iam_variation_vec4_fn)(int index, void* user);

// Float variation
struct iam_variation_float {
	int                     mode;
	float                   amount;
	float                   min_clamp;
	float                   max_clamp;
	unsigned int            seed;       // 0 = global random, non-zero = deterministic
	iam_variation_float_fn  callback;
	void*                   user;
};

// Int variation
struct iam_variation_int {
	int                   mode;
	int                   amount;
	int                   min_clamp;
	int                   max_clamp;
	unsigned int          seed;
	iam_variation_int_fn  callback;
	void*                 user;
};

// Vec2 variation (global mode or per-axis)
struct iam_variation_vec2 {
	int                    mode;        // Global mode (iam_var_none = use per-axis)
	ImVec2                 amount;
	ImVec2                 min_clamp;
	ImVec2                 max_clamp;
	unsigned int           seed;
	iam_variation_vec2_fn  callback;
	void*                  user;
	iam_variation_float    x, y;        // Per-axis (used when mode == iam_var_none)
};

// Vec4 variation (global mode or per-axis)
struct iam_variation_vec4 {
	int                    mode;
	ImVec4                 amount;
	ImVec4                 min_clamp;
	ImVec4                 max_clamp;
	unsigned int           seed;
	iam_variation_vec4_fn  callback;
	void*                  user;
	iam_variation_float    x, y, z, w;  // Per-axis
};

// Color variation (global mode or per-channel)
struct iam_variation_color {
	int                    mode;
	ImVec4                 amount;
	ImVec4                 min_clamp;
	ImVec4                 max_clamp;
	int                    color_space; // iam_col_oklab, etc.
	unsigned int           seed;
	iam_variation_vec4_fn  callback;    // Returns ImVec4 color delta
	void*                  user;
	iam_variation_float    r, g, b, a;  // Per-channel
};

// ----------------------------------------------------
// Variation helper functions (C11-style inline)
// ----------------------------------------------------

// Float variation helpers
static inline iam_variation_float iam_varf_none(void) {
	iam_variation_float v = {iam_var_none, 0, -FLT_MAX, FLT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_float iam_varf_inc(float amt) {
	iam_variation_float v = {iam_var_increment, amt, -FLT_MAX, FLT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_float iam_varf_dec(float amt) {
	iam_variation_float v = {iam_var_decrement, amt, -FLT_MAX, FLT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_float iam_varf_mul(float f) {
	iam_variation_float v = {iam_var_multiply, f, -FLT_MAX, FLT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_float iam_varf_rand(float r) {
	iam_variation_float v = {iam_var_random, r, -FLT_MAX, FLT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_float iam_varf_rand_abs(float r) {
	iam_variation_float v = {iam_var_random_abs, r, -FLT_MAX, FLT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_float iam_varf_pingpong(float amt) {
	iam_variation_float v = {iam_var_pingpong, amt, -FLT_MAX, FLT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_float iam_varf_fn(iam_variation_float_fn fn, void* user) {
	iam_variation_float v = {iam_var_callback, 0, -FLT_MAX, FLT_MAX, 0, fn, user};
	return v;
}
static inline iam_variation_float iam_varf_clamp(iam_variation_float v, float mn, float mx) {
	v.min_clamp = mn; v.max_clamp = mx; return v;
}
static inline iam_variation_float iam_varf_seed(iam_variation_float v, unsigned int s) {
	v.seed = s; return v;
}

// Int variation helpers
static inline iam_variation_int iam_vari_none(void) {
	iam_variation_int v = {iam_var_none, 0, INT_MIN, INT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_int iam_vari_inc(int amt) {
	iam_variation_int v = {iam_var_increment, amt, INT_MIN, INT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_int iam_vari_dec(int amt) {
	iam_variation_int v = {iam_var_decrement, amt, INT_MIN, INT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_int iam_vari_rand(int r) {
	iam_variation_int v = {iam_var_random, r, INT_MIN, INT_MAX, 0, 0, 0};
	return v;
}
static inline iam_variation_int iam_vari_fn(iam_variation_int_fn fn, void* user) {
	iam_variation_int v = {iam_var_callback, 0, INT_MIN, INT_MAX, 0, fn, user};
	return v;
}
static inline iam_variation_int iam_vari_clamp(iam_variation_int v, int mn, int mx) {
	v.min_clamp = mn; v.max_clamp = mx; return v;
}
static inline iam_variation_int iam_vari_seed(iam_variation_int v, unsigned int s) {
	v.seed = s; return v;
}

// Vec2 variation helpers (global)
static inline iam_variation_vec2 iam_varv2_none(void) {
	iam_variation_vec2 v = {iam_var_none, {0,0}, {-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX}, 0, 0, 0, {0}, {0}};
	return v;
}
static inline iam_variation_vec2 iam_varv2_inc(float x, float y) {
	iam_variation_vec2 v = {iam_var_increment, {x,y}, {-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX}, 0, 0, 0, {0}, {0}};
	return v;
}
static inline iam_variation_vec2 iam_varv2_dec(float x, float y) {
	iam_variation_vec2 v = {iam_var_decrement, {x,y}, {-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX}, 0, 0, 0, {0}, {0}};
	return v;
}
static inline iam_variation_vec2 iam_varv2_mul(float f) {
	iam_variation_vec2 v = {iam_var_multiply, {f,f}, {-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX}, 0, 0, 0, {0}, {0}};
	return v;
}
static inline iam_variation_vec2 iam_varv2_rand(float x, float y) {
	iam_variation_vec2 v = {iam_var_random, {x,y}, {-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX}, 0, 0, 0, {0}, {0}};
	return v;
}
static inline iam_variation_vec2 iam_varv2_fn(iam_variation_vec2_fn fn, void* user) {
	iam_variation_vec2 v = {iam_var_callback, {0,0}, {-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX}, 0, fn, user, {0}, {0}};
	return v;
}
// Vec2 per-axis helper
static inline iam_variation_vec2 iam_varv2_axis(iam_variation_float vx, iam_variation_float vy) {
	iam_variation_vec2 v = {iam_var_none, {0,0}, {-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX}, 0, 0, 0, vx, vy};
	return v;
}
static inline iam_variation_vec2 iam_varv2_clamp(iam_variation_vec2 v, ImVec2 mn, ImVec2 mx) {
	v.min_clamp = mn; v.max_clamp = mx; return v;
}
static inline iam_variation_vec2 iam_varv2_seed(iam_variation_vec2 v, unsigned int s) {
	v.seed = s; return v;
}

// Vec4 variation helpers (global)
static inline iam_variation_vec4 iam_varv4_none(void) {
	iam_variation_vec4 v = {iam_var_none, {0,0,0,0}, {-FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX}, 0, 0, 0, {0}, {0}, {0}, {0}};
	return v;
}
static inline iam_variation_vec4 iam_varv4_inc(float x, float y, float z, float w) {
	iam_variation_vec4 v = {iam_var_increment, {x,y,z,w}, {-FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX}, 0, 0, 0, {0}, {0}, {0}, {0}};
	return v;
}
static inline iam_variation_vec4 iam_varv4_dec(float x, float y, float z, float w) {
	iam_variation_vec4 v = {iam_var_decrement, {x,y,z,w}, {-FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX}, 0, 0, 0, {0}, {0}, {0}, {0}};
	return v;
}
static inline iam_variation_vec4 iam_varv4_mul(float f) {
	iam_variation_vec4 v = {iam_var_multiply, {f,f,f,f}, {-FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX}, 0, 0, 0, {0}, {0}, {0}, {0}};
	return v;
}
static inline iam_variation_vec4 iam_varv4_rand(float x, float y, float z, float w) {
	iam_variation_vec4 v = {iam_var_random, {x,y,z,w}, {-FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX}, 0, 0, 0, {0}, {0}, {0}, {0}};
	return v;
}
static inline iam_variation_vec4 iam_varv4_fn(iam_variation_vec4_fn fn, void* user) {
	iam_variation_vec4 v = {iam_var_callback, {0,0,0,0}, {-FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX}, 0, fn, user, {0}, {0}, {0}, {0}};
	return v;
}
// Vec4 per-axis helper
static inline iam_variation_vec4 iam_varv4_axis(iam_variation_float vx, iam_variation_float vy, iam_variation_float vz, iam_variation_float vw) {
	iam_variation_vec4 v = {iam_var_none, {0,0,0,0}, {-FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX}, {FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX}, 0, 0, 0, vx, vy, vz, vw};
	return v;
}
static inline iam_variation_vec4 iam_varv4_clamp(iam_variation_vec4 v, ImVec4 mn, ImVec4 mx) {
	v.min_clamp = mn; v.max_clamp = mx; return v;
}
static inline iam_variation_vec4 iam_varv4_seed(iam_variation_vec4 v, unsigned int s) {
	v.seed = s; return v;
}

// Color variation helpers (global)
static inline iam_variation_color iam_varc_none(void) {
	iam_variation_color v = {iam_var_none, {0,0,0,0}, {0,0,0,0}, {1,1,1,1}, iam_col_oklab, 0, 0, 0, {0}, {0}, {0}, {0}};
	return v;
}
static inline iam_variation_color iam_varc_inc(float r, float g, float b, float a) {
	iam_variation_color v = {iam_var_increment, {r,g,b,a}, {0,0,0,0}, {1,1,1,1}, iam_col_oklab, 0, 0, 0, {0}, {0}, {0}, {0}};
	return v;
}
static inline iam_variation_color iam_varc_dec(float r, float g, float b, float a) {
	iam_variation_color v = {iam_var_decrement, {r,g,b,a}, {0,0,0,0}, {1,1,1,1}, iam_col_oklab, 0, 0, 0, {0}, {0}, {0}, {0}};
	return v;
}
static inline iam_variation_color iam_varc_mul(float f) {
	iam_variation_color v = {iam_var_multiply, {f,f,f,1}, {0,0,0,0}, {1,1,1,1}, iam_col_oklab, 0, 0, 0, {0}, {0}, {0}, {0}};
	return v;
}
static inline iam_variation_color iam_varc_rand(float r, float g, float b, float a) {
	iam_variation_color v = {iam_var_random, {r,g,b,a}, {0,0,0,0}, {1,1,1,1}, iam_col_oklab, 0, 0, 0, {0}, {0}, {0}, {0}};
	return v;
}
static inline iam_variation_color iam_varc_fn(iam_variation_vec4_fn fn, void* user) {
	iam_variation_color v = {iam_var_callback, {0,0,0,0}, {0,0,0,0}, {1,1,1,1}, iam_col_oklab, 0, fn, user, {0}, {0}, {0}, {0}};
	return v;
}
// Color per-channel helper
static inline iam_variation_color iam_varc_channel(iam_variation_float vr, iam_variation_float vg, iam_variation_float vb, iam_variation_float va) {
	iam_variation_color v = {iam_var_none, {0,0,0,0}, {0,0,0,0}, {1,1,1,1}, iam_col_oklab, 0, 0, 0, vr, vg, vb, va};
	return v;
}
static inline iam_variation_color iam_varc_space(iam_variation_color v, int space) {
	v.color_space = space; return v;
}
static inline iam_variation_color iam_varc_clamp(iam_variation_color v, ImVec4 mn, ImVec4 mx) {
	v.min_clamp = mn; v.max_clamp = mx; return v;
}
static inline iam_variation_color iam_varc_seed(iam_variation_color v, unsigned int s) {
	v.seed = s; return v;
}

// Forward declarations
struct iam_clip_data;
struct iam_instance_data;

// Callback types (use instance ID instead of pointer for safety)
typedef void (*iam_clip_callback)(ImGuiID inst_id, void* user_data);

// Marker callback (includes marker name/id for identification)
typedef void (*iam_marker_callback)(ImGuiID inst_id, ImGuiID marker_id, float marker_time, void* user_data);

// ----------------------------------------------------
// iam_clip - fluent API for authoring animations
// ----------------------------------------------------
class iam_clip {
public:
	// Start building a new clip with the given ID
	static iam_clip begin(ImGuiID clip_id);

	// Add keyframes for different channel types
	iam_clip& key_float(ImGuiID channel, float time, float value, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);
	iam_clip& key_vec2(ImGuiID channel, float time, ImVec2 value, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);
	iam_clip& key_vec4(ImGuiID channel, float time, ImVec4 value, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);
	iam_clip& key_int(ImGuiID channel, float time, int value, int ease_type = iam_ease_linear);
	iam_clip& key_color(ImGuiID channel, float time, ImVec4 value, int color_space = iam_col_oklab, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);

	// Keyframes with repeat variation (value changes per loop iteration)
	iam_clip& key_float_var(ImGuiID channel, float time, float value, iam_variation_float const& var, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);
	iam_clip& key_vec2_var(ImGuiID channel, float time, ImVec2 value, iam_variation_vec2 const& var, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);
	iam_clip& key_vec4_var(ImGuiID channel, float time, ImVec4 value, iam_variation_vec4 const& var, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);
	iam_clip& key_int_var(ImGuiID channel, float time, int value, iam_variation_int const& var, int ease_type = iam_ease_linear);
	iam_clip& key_color_var(ImGuiID channel, float time, ImVec4 value, iam_variation_color const& var, int color_space = iam_col_oklab, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);

	// Spring-based keyframe (float only)
	iam_clip& key_float_spring(ImGuiID channel, float time, float target, iam_spring_params const& spring);

	// Anchor-relative keyframes (values resolved relative to window/viewport at get time)
	iam_clip& key_float_rel(ImGuiID channel, float time, float percent, float px_bias, int anchor_space, int axis, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);
	iam_clip& key_vec2_rel(ImGuiID channel, float time, ImVec2 percent, ImVec2 px_bias, int anchor_space, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);
	iam_clip& key_vec4_rel(ImGuiID channel, float time, ImVec4 percent, ImVec4 px_bias, int anchor_space, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);
	iam_clip& key_color_rel(ImGuiID channel, float time, ImVec4 percent, ImVec4 px_bias, int color_space, int anchor_space, int ease_type = iam_ease_linear, float const* bezier4 = nullptr);

	// Timeline grouping - sequential and parallel keyframe blocks
	iam_clip& seq_begin();  // Start sequential block (keyframes after seq_end start after this block)
	iam_clip& seq_end();
	iam_clip& par_begin();  // Start parallel block (keyframes play at same time offset)
	iam_clip& par_end();

	// Timeline markers - callbacks at specific times during playback
	iam_clip& marker(float time, ImGuiID marker_id, iam_marker_callback cb, void* user = nullptr);  // Add marker at specific time.
	iam_clip& marker(float time, iam_marker_callback cb, void* user = nullptr);                     // Add marker (auto-generated ID).

	// Clip options
	iam_clip& set_loop(bool loop, int direction = iam_dir_normal, int loop_count = -1);
	iam_clip& set_delay(float delay_seconds);
	iam_clip& set_stagger(int count, float each_delay, float from_center_bias = 0.0f);

	// Timing variation per loop iteration
	iam_clip& set_duration_var(iam_variation_float const& var);   // Vary clip duration per loop
	iam_clip& set_delay_var(iam_variation_float const& var);      // Vary delay between loops
	iam_clip& set_timescale_var(iam_variation_float const& var);  // Vary playback speed per loop

	// Callbacks
	iam_clip& on_begin(iam_clip_callback cb, void* user = nullptr);
	iam_clip& on_update(iam_clip_callback cb, void* user = nullptr);
	iam_clip& on_complete(iam_clip_callback cb, void* user = nullptr);

	// Finalize the clip
	void end();

	// Get the clip ID
	ImGuiID id() const { return m_clip_id; }

private:
	iam_clip(ImGuiID clip_id) : m_clip_id(clip_id) {}
	ImGuiID m_clip_id;
};

// ----------------------------------------------------
// iam_instance - playback control for a clip
// ----------------------------------------------------
class iam_instance {
public:
	iam_instance() : m_inst_id(0) {}
	iam_instance(ImGuiID inst_id) : m_inst_id(inst_id) {}

	// Playback control
	void pause();
	void resume();
	void stop();
	void destroy();  // Remove instance from system (valid() will return false after this)
	void seek(float time);
	void set_time_scale(float scale);
	void set_weight(float weight);  // for layering/blending

	// Animation chaining - play another clip when this one completes
	iam_instance& then(ImGuiID next_clip_id);                                        // Chain another clip to play after this one.
	iam_instance& then(ImGuiID next_clip_id, ImGuiID next_instance_id);              // Chain with specific instance ID.
	iam_instance& then_delay(float delay);                                           // Set delay before chained clip starts.

	// Query state
	float time() const;
	float duration() const;
	bool is_playing() const;
	bool is_paused() const;

	// Get animated values
	bool get_float(ImGuiID channel, float* out) const;
	bool get_vec2(ImGuiID channel, ImVec2* out) const;
	bool get_vec4(ImGuiID channel, ImVec4* out) const;
	bool get_int(ImGuiID channel, int* out) const;
	bool get_color(ImGuiID channel, ImVec4* out, int color_space = iam_col_oklab) const;  // Color blended in specified color space.

	// Check validity
	bool valid() const;
	operator bool() const { return valid(); }

	ImGuiID id() const { return m_inst_id; }

private:
	ImGuiID m_inst_id;
};

// ----------------------------------------------------
// Clip System API
// ----------------------------------------------------

// Initialize/shutdown (optional - auto-init on first use)
void iam_clip_init(int initial_clip_cap = 256, int initial_inst_cap = 4096);
void iam_clip_shutdown();

// Per-frame update (call after iam_update_begin_frame)
void iam_clip_update(float dt);

// Garbage collection for instances
void iam_clip_gc(unsigned int max_age_frames = 600);

// Play a clip on an instance (creates or reuses instance)
iam_instance iam_play(ImGuiID clip_id, ImGuiID instance_id);

// Get an existing instance (returns invalid iam_instance if not found)
iam_instance iam_get_instance(ImGuiID instance_id);

// Query clip info
float iam_clip_duration(ImGuiID clip_id);                                       // Get clip duration in seconds.
bool iam_clip_exists(ImGuiID clip_id);                                          // Check if clip exists.

// Stagger helpers - compute delay for indexed instances
float iam_stagger_delay(ImGuiID clip_id, int index);                            // Get stagger delay for element at index.
iam_instance iam_play_stagger(ImGuiID clip_id, ImGuiID instance_id, int index); // Play with stagger delay applied.

// Layering support - blend multiple animation instances
void iam_layer_begin(ImGuiID instance_id);                                      // Start blending into target instance.
void iam_layer_add(iam_instance inst, float weight);                            // Add source instance with weight.
void iam_layer_end(ImGuiID instance_id);                                        // Finalize blending and normalize weights.
bool iam_get_blended_float(ImGuiID instance_id, ImGuiID channel, float* out);   // Get blended float value.
bool iam_get_blended_vec2(ImGuiID instance_id, ImGuiID channel, ImVec2* out);   // Get blended vec2 value.
bool iam_get_blended_vec4(ImGuiID instance_id, ImGuiID channel, ImVec4* out);   // Get blended vec4 value.
bool iam_get_blended_int(ImGuiID instance_id, ImGuiID channel, int* out);       // Get blended int value.

// Persistence (optional)
iam_result iam_clip_save(ImGuiID clip_id, char const* path);
iam_result iam_clip_load(char const* path, ImGuiID* out_clip_id);

// ----------------------------------------------------
// Usage notes (summary)
// ----------------------------------------------------
// TWEEN API:
// 1) Call iam_update_begin_frame() once per frame; use ImGui::GetIO().DeltaTime as dt.
// 2) For each widget/object, pick a stable ImGuiID (e.g. ImGui::GetItemID()) and a channel_id (ImHashStr("alpha")).
// 3) Call iam_tween_* to get the animated value, then apply it (PushStyleVar, etc).
// 4) Optionally call iam_gc(600) every second to bound memory.
// 5) Use iam_tween_vec2_rel / iam_tween_vec2_resolved / iam_rebase_vec2 to keep animations smooth on window/dock/viewport changes.
//
// CLIP API:
// 1) Author clips once at startup using iam_clip::begin(id).key_*(...).end()
// 2) Call iam_clip_update(dt) each frame after iam_update_begin_frame()
// 3) Use iam_play(clip_id, instance_id) to start playback; returns iam_instance for queries
// 4) Call inst.get_float/vec2/vec4/int() to sample animated values
// 5) Optionally call iam_clip_gc(600) to bound instance memory
