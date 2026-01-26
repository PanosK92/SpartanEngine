// im_anim_doc.cpp — Complete ImAnim Documentation & Interactive Examples
// Author: Soufiane KHIAT
// License: MIT
//
// This file provides comprehensive documentation for all ImAnim features.
// Each section includes explanations and interactive examples.
//
// Usage: Call ImAnimDocWindow() inside your Dear ImGui frame.

#include "pch.h"
#include "im_anim.h"
#include "../../Source/imgui.h"
#include "../../Source/imgui_internal.h"
#include <math.h>
#include <stdio.h>

// ============================================================
// HELPER: Get delta time with safety bounds
// ============================================================
static float GetDocDeltaTime()
{
	float dt = ImGui::GetIO().DeltaTime;
	if (dt <= 0.0f) dt = 1.0f / 60.0f;
	if (dt > 0.1f) dt = 0.1f;
	return dt;
}

// ============================================================
// HELPER: Open/Close all collapsing headers
// ============================================================
static int s_doc_open_all = 0;  // 0 = none, 1 = open all, -1 = close all

static void DocApplyOpenAll()
{
	if (s_doc_open_all != 0)
		ImGui::SetNextItemOpen(s_doc_open_all > 0, ImGuiCond_Always);
}

// ============================================================
// HELPER: Section header with description
// ============================================================
static void DocSectionHeader(char const* title, char const* description)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.9f, 1.0f));
	ImGui::TextUnformatted(title);
	ImGui::PopStyleColor();
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
	ImGui::TextWrapped("%s", description);
	ImGui::PopStyleColor();
	ImGui::Spacing();
}

// ============================================================
// HELPER: Code snippet display (copy-pastable)
// ============================================================
static void DocCodeSnippet(char const* code)
{
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.12f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.5f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

	// Calculate size based on content
	ImVec2 size = ImGui::CalcTextSize(code);
	float height = size.y + 24.0f;  // Extra padding to avoid scrollbar

	// Use InputTextMultiline with ReadOnly for copy-paste support
	ImGui::PushID(code);  // Unique ID based on code pointer
	ImGui::InputTextMultiline(
		"##code",
		(char*)code,
		strlen(code) + 1,
		ImVec2(-FLT_MIN, height),
		ImGuiInputTextFlags_ReadOnly
	);
	ImGui::PopID();

	ImGui::PopStyleVar();
	ImGui::PopStyleColor(2);
}

// ============================================================
// CLIP IDs for documentation examples
// ============================================================

// Tween type examples
static ImGuiID const DOC_CLIP_FLOAT = ImHashStr("doc_clip_float");
static ImGuiID const DOC_CLIP_VEC2 = ImHashStr("doc_clip_vec2");
static ImGuiID const DOC_CLIP_VEC4 = ImHashStr("doc_clip_vec4");
static ImGuiID const DOC_CLIP_INT = ImHashStr("doc_clip_int");
static ImGuiID const DOC_CLIP_COLOR = ImHashStr("doc_clip_color");

// Policy examples
static ImGuiID const DOC_CLIP_CROSSFADE = ImHashStr("doc_clip_crossfade");
static ImGuiID const DOC_CLIP_CUT = ImHashStr("doc_clip_cut");
static ImGuiID const DOC_CLIP_QUEUE = ImHashStr("doc_clip_queue");

// Easing examples
static ImGuiID const DOC_CLIP_EASE_PRESET = ImHashStr("doc_clip_ease_preset");
static ImGuiID const DOC_CLIP_EASE_BEZIER = ImHashStr("doc_clip_ease_bezier");
static ImGuiID const DOC_CLIP_EASE_SPRING = ImHashStr("doc_clip_ease_spring");
static ImGuiID const DOC_CLIP_EASE_STEPS = ImHashStr("doc_clip_ease_steps");

// Color space examples
static ImGuiID const DOC_CLIP_COLOR_SRGB = ImHashStr("doc_clip_color_srgb");
static ImGuiID const DOC_CLIP_COLOR_OKLAB = ImHashStr("doc_clip_color_oklab");
static ImGuiID const DOC_CLIP_COLOR_HSV = ImHashStr("doc_clip_color_hsv");

// Clip features
static ImGuiID const DOC_CLIP_LOOP = ImHashStr("doc_clip_loop");
static ImGuiID const DOC_CLIP_DELAY = ImHashStr("doc_clip_delay");
static ImGuiID const DOC_CLIP_TIMING_VAR = ImHashStr("doc_clip_timing_var");
static ImGuiID const DOC_CLIP_STAGGER = ImHashStr("doc_clip_stagger");
static ImGuiID const DOC_CLIP_MARKERS = ImHashStr("doc_clip_markers");
static ImGuiID const DOC_CLIP_CHAINING = ImHashStr("doc_clip_chaining");
static ImGuiID const DOC_CLIP_CHAIN_A = ImHashStr("doc_clip_chain_a");
static ImGuiID const DOC_CLIP_CHAIN_B = ImHashStr("doc_clip_chain_b");
static ImGuiID const DOC_CLIP_VARIATION = ImHashStr("doc_clip_variation");
static ImGuiID const DOC_CLIP_SEQ_PAR = ImHashStr("doc_clip_seq_par");

// Path examples
static ImGuiID const DOC_PATH_LINE = ImHashStr("doc_path_line");
static ImGuiID const DOC_PATH_BEZIER = ImHashStr("doc_path_bezier");
static ImGuiID const DOC_PATH_CATMULL = ImHashStr("doc_path_catmull");

// Channel IDs
static ImGuiID const DOC_CH_VALUE = ImHashStr("doc_ch_value");
static ImGuiID const DOC_CH_POS = ImHashStr("doc_ch_pos");
static ImGuiID const DOC_CH_COLOR = ImHashStr("doc_ch_color");
static ImGuiID const DOC_CH_X = ImHashStr("doc_ch_x");
static ImGuiID const DOC_CH_Y = ImHashStr("doc_ch_y");
static ImGuiID const DOC_CH_SCALE = ImHashStr("doc_ch_scale");
static ImGuiID const DOC_CH_ALPHA = ImHashStr("doc_ch_alpha");
static ImGuiID const DOC_CH_ROTATION = ImHashStr("doc_ch_rotation");

// ============================================================
// SECTION 1: TWEEN API - VALUE TYPES
// ============================================================

static void DocSection_TweenTypes()
{
	DocSectionHeader("TWEEN API - Value Types",
		"The Tween API provides frame-by-frame value interpolation. Call the tween function "
		"every frame with your target value, and it returns the smoothly animated current value. "
		"Five value types are supported: float, vec2, vec4, int, and color.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Float Tween
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_tween_float"))
	{
		ImGui::TextWrapped(
			"Animates a single floating-point value. Most common use case for opacity, "
			"scale, rotation angles, progress bars, etc.");

		DocCodeSnippet(
			"float alpha = iam_tween_float(\n"
			"    id,           // ImGuiID - unique identifier\n"
			"    channel_id,   // ImGuiID - channel within the id\n"
			"    target,       // float - target value\n"
			"    duration,     // float - seconds to reach target\n"
			"    ease,         // iam_ease_desc - easing curve\n"
			"    policy,       // int - iam_policy_crossfade/cut/queue\n"
			"    dt,           // float - delta time\n"
			"    init_value    // float - initial value (default: 0)\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example:");

		static float target = 1.0f;
		static bool toggle = false;
		if (ImGui::Button("Toggle Target##float")) {
			toggle = !toggle;
			target = toggle ? 0.0f : 1.0f;
		}
		ImGui::SameLine();
		ImGui::Text("Target: %.1f", target);

		ImGuiID id = ImGui::GetID("float_tween_demo");
		float value = iam_tween_float(id, DOC_CH_VALUE, target, 0.5f,
			iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt);

		ImGui::ProgressBar(value, ImVec2(-1, 0), "");
		ImGui::Text("Current Value: %.4f", value);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Vec2 Tween
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_tween_vec2"))
	{
		ImGui::TextWrapped(
			"Animates a 2D vector (ImVec2). Perfect for positions, sizes, UV coordinates, "
			"or any paired values that should animate together.");

		DocCodeSnippet(
			"ImVec2 pos = iam_tween_vec2(\n"
			"    id, channel_id,\n"
			"    ImVec2(target_x, target_y),\n"
			"    duration, ease, policy, dt,\n"
			"    init_value  // ImVec2 - initial value (default: 0,0)\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example:");

		static ImVec2 target_pos(50.0f, 50.0f);  // Start at corner 0
		static int corner = 0;
		ImVec2 corners[] = {
			ImVec2(50.0f, 50.0f),
			ImVec2(200.0f, 50.0f),
			ImVec2(200.0f, 100.0f),
			ImVec2(50.0f, 100.0f)
		};

		if (ImGui::Button("Next Corner##vec2")) {
			corner = (corner + 1) % 4;
			target_pos = corners[corner];
		}
		ImGui::SameLine();
		ImGui::Text("Target: (%.0f, %.0f)", target_pos.x, target_pos.y);

		ImGuiID id = ImGui::GetID("vec2_tween_demo");
		ImVec2 pos = iam_tween_vec2(id, DOC_CH_POS, target_pos, 0.4f,
			iam_ease_preset(iam_ease_out_back), iam_policy_crossfade, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(260.0f, 160.0f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		ImVec2 dot_pos(canvas_pos.x + pos.x, canvas_pos.y + pos.y);
		dl->AddCircleFilled(dot_pos, 12.0f, IM_COL32(91, 194, 231, 255));
		dl->AddCircle(dot_pos, 14.0f, IM_COL32(255, 255, 255, 100), 0, 2.0f);

		ImGui::Dummy(canvas_size);
		ImGui::Text("Current: (%.1f, %.1f)", pos.x, pos.y);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Vec4 Tween
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_tween_vec4"))
	{
		ImGui::TextWrapped(
			"Animates a 4D vector (ImVec4). Useful for rectangles (x, y, w, h), "
			"quaternions, or any four related values. For colors, prefer iam_tween_color.");

		DocCodeSnippet(
			"ImVec4 rect = iam_tween_vec4(\n"
			"    id, channel_id,\n"
			"    ImVec4(x, y, w, h),\n"
			"    duration, ease, policy, dt,\n"
			"    init_value  // ImVec4 - initial value (default: 0,0,0,0)\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example:");

		static ImVec4 target_rect(20.0f, 20.0f, 60.0f, 40.0f);
		static int rect_state = 0;
		ImVec4 rects[] = {
			ImVec4(20.0f, 20.0f, 60.0f, 40.0f),
			ImVec4(100.0f, 30.0f, 100.0f, 80.0f),
			ImVec4(50.0f, 60.0f, 150.0f, 50.0f)
		};

		if (ImGui::Button("Next Rect##vec4")) {
			rect_state = (rect_state + 1) % 3;
			target_rect = rects[rect_state];
		}

		ImGuiID id = ImGui::GetID("vec4_tween_demo");
		ImVec4 rect = iam_tween_vec4(id, DOC_CH_VALUE, target_rect, 0.5f,
			iam_ease_preset(iam_ease_out_elastic), iam_policy_crossfade, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(260.0f, 140.0f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		dl->AddRectFilled(
			ImVec2(canvas_pos.x + rect.x, canvas_pos.y + rect.y),
			ImVec2(canvas_pos.x + rect.x + rect.z, canvas_pos.y + rect.y + rect.w),
			IM_COL32(204, 120, 88, 200), 4.0f);

		ImGui::Dummy(canvas_size);
		ImGui::Text("Rect: (%.1f, %.1f, %.1f, %.1f)", rect.x, rect.y, rect.z, rect.w);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Int Tween
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_tween_int"))
	{
		ImGui::TextWrapped(
			"Animates an integer value with smooth interpolation. The internal calculation "
			"uses floats, but the result is rounded. Great for counters, scores, indices.");

		DocCodeSnippet(
			"int count = iam_tween_int(\n"
			"    id, channel_id,\n"
			"    target_int,\n"
			"    duration, ease, policy, dt,\n"
			"    init_value  // int - initial value (default: 0)\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example:");

		static int target_int = 100;
		if (ImGui::Button("-50##int")) target_int -= 50;
		ImGui::SameLine();
		if (ImGui::Button("+50##int")) target_int += 50;
		ImGui::SameLine();
		if (ImGui::Button("Random##int")) target_int = rand() % 1000;
		ImGui::SameLine();
		ImGui::Text("Target: %d", target_int);

		ImGuiID id = ImGui::GetID("int_tween_demo");
		int value = iam_tween_int(id, DOC_CH_VALUE, target_int, 0.8f,
			iam_ease_preset(iam_ease_out_expo), iam_policy_crossfade, dt);

		ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
		ImGui::SetWindowFontScale(2.0f);
		ImGui::Text("%d", value);
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopFont();

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Color Tween
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_tween_color"))
	{
		ImGui::TextWrapped(
			"Animates colors with proper color space interpolation. Unlike vec4, this function "
			"understands color theory and can blend in sRGB, linear, HSV, OKLAB, or OKLCH space.");

		DocCodeSnippet(
			"ImVec4 color = iam_tween_color(\n"
			"    id, channel_id,\n"
			"    target_color,     // ImVec4 in sRGB\n"
			"    duration, ease, policy,\n"
			"    color_space,      // iam_col_srgb/oklab/hsv/etc\n"
			"    dt,\n"
			"    init_value        // ImVec4 - initial value (default: white)\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example:");

		static ImVec4 target_color(1.0f, 0.0f, 0.0f, 1.0f);
		static int color_idx = 0;
		ImVec4 colors[] = {
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f),  // Red
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f),  // Green
			ImVec4(0.0f, 0.0f, 1.0f, 1.0f),  // Blue
			ImVec4(1.0f, 1.0f, 0.0f, 1.0f),  // Yellow
			ImVec4(1.0f, 0.0f, 1.0f, 1.0f),  // Magenta
		};

		if (ImGui::Button("Next Color##color")) {
			color_idx = (color_idx + 1) % 5;
			target_color = colors[color_idx];
		}
		ImGui::SameLine();
		ImGui::ColorButton("Target", target_color, 0, ImVec2(60, 20));

		ImGuiID id = ImGui::GetID("color_tween_demo");
		ImVec4 color = iam_tween_color(id, DOC_CH_COLOR, target_color, 0.6f,
			iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, iam_col_oklab, dt);

		ImGui::ColorButton("Current (OKLAB blend)", color, 0, ImVec2(200, 40));

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 2: TWEEN API - POLICIES
// ============================================================

static void DocSection_TweenPolicies()
{
	DocSectionHeader("TWEEN API - Policies",
		"Policies control how the animation responds when the target changes mid-animation. "
		"Choose the right policy for your use case.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Crossfade Policy
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_policy_crossfade"))
	{
		ImGui::TextWrapped(
			"DEFAULT. Smoothly transitions to the new target from the current position. "
			"The animation restarts with the current value as the new start point.");

		DocCodeSnippet("policy = iam_policy_crossfade;  // Smooth redirect");

		ImGui::Separator();

		static float target = 1.0f;
		if (ImGui::Button("Toggle##crossfade")) target = (target > 0.5f) ? 0.0f : 1.0f;
		ImGui::SameLine();
		ImGui::Text("Click rapidly to see smooth transitions");

		ImGuiID id = ImGui::GetID("crossfade_demo");
		float value = iam_tween_float(id, DOC_CH_VALUE, target, 4.0f,
			iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt);

		ImGui::ProgressBar(value, ImVec2(-1, 20), "Crossfade");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Cut Policy
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_policy_cut"))
	{
		ImGui::TextWrapped(
			"Instantly snaps to the new target. No animation. Useful for state resets "
			"or when you need immediate response.");

		DocCodeSnippet("policy = iam_policy_cut;  // Instant snap");

		ImGui::Separator();

		static float target = 1.0f;
		if (ImGui::Button("Toggle##cut")) target = (target > 0.5f) ? 0.0f : 1.0f;
		ImGui::SameLine();
		ImGui::Text("Notice the instant change");

		ImGuiID id = ImGui::GetID("cut_demo");
		float value = iam_tween_float(id, DOC_CH_VALUE, target, 4.0f,
			iam_ease_preset(iam_ease_out_cubic), iam_policy_cut, dt);

		ImGui::ProgressBar(value, ImVec2(-1, 20), "Cut");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Queue Policy
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_policy_queue"))
	{
		ImGui::TextWrapped(
			"Queues the new target to start after the current animation completes. "
			"Only one pending target is stored (latest overwrites previous).");

		DocCodeSnippet("policy = iam_policy_queue;  // Wait for current to finish");

		ImGui::Separator();

		static float target = 1.0f;
		static int click_count = 0;
		if (ImGui::Button("Queue Toggle##queue")) {
			target = (target > 0.5f) ? 0.0f : 1.0f;
			click_count++;
		}
		ImGui::SameLine();
		ImGui::Text("Click count: %d (animation queues)", click_count);

		ImGuiID id = ImGui::GetID("queue_demo");
		float value = iam_tween_float(id, DOC_CH_VALUE, target, 3.0f,
			iam_ease_preset(iam_ease_in_out_cubic), iam_policy_queue, dt);

		ImGui::ProgressBar(value, ImVec2(-1, 20), "Queue");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 3: EASING FUNCTIONS
// ============================================================

static void DocSection_Easing()
{
	DocSectionHeader("EASING FUNCTIONS",
		"Easing functions control the rate of change over time. ImAnim provides 31 presets, "
		"parametric curves (bezier, steps, spring), and custom function slots.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Preset Easings
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Preset Easings (31 types)"))
	{
		ImGui::TextWrapped(
			"Standard easing functions organized by family (quad, cubic, quart, quint, sine, "
			"expo, circ, back, elastic, bounce) with in/out/in-out variants.");

		DocCodeSnippet(
			"iam_ease_desc ease = iam_ease_preset(iam_ease_out_cubic);\n"
			"// or: iam_ease_preset(iam_ease_in_out_elastic)");

		ImGui::Separator();

		static int selected_ease = iam_ease_out_cubic;
		char const* ease_names[] = {
			"linear",
			"in_quad", "out_quad", "in_out_quad",
			"in_cubic", "out_cubic", "in_out_cubic",
			"in_quart", "out_quart", "in_out_quart",
			"in_quint", "out_quint", "in_out_quint",
			"in_sine", "out_sine", "in_out_sine",
			"in_expo", "out_expo", "in_out_expo",
			"in_circ", "out_circ", "in_out_circ",
			"in_back", "out_back", "in_out_back",
			"in_elastic", "out_elastic", "in_out_elastic",
			"in_bounce", "out_bounce", "in_out_bounce"
		};
		ImGui::Combo("Easing", &selected_ease, ease_names, IM_ARRAYSIZE(ease_names));

		static float target = 1.0f;
		static float timer = 0.0f;
		timer += dt;
		if (timer > 2.0f) {
			timer = 0.0f;
			target = (target > 0.5f) ? 0.0f : 1.0f;
		}

		ImGuiID id = ImGui::GetID("preset_ease_demo");
		float value = iam_tween_float(id, DOC_CH_VALUE, target, 1.0f,
			iam_ease_preset(selected_ease), iam_policy_crossfade, dt);

		// Draw easing curve
		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(200.0f, 100.0f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw curve
		ImVec2 prev(canvas_pos.x, canvas_pos.y + canvas_size.y);
		for (int i = 1; i <= 50; i++) {
			float t = (float)i / 50.0f;
			float y = iam_eval_preset(selected_ease, t);
			ImVec2 curr(canvas_pos.x + t * canvas_size.x, canvas_pos.y + canvas_size.y - y * canvas_size.y);
			dl->AddLine(prev, curr, IM_COL32(91, 194, 231, 255), 2.0f);
			prev = curr;
		}

		// Draw current position
		float curve_y = iam_eval_preset(selected_ease, value);
		ImVec2 dot(canvas_pos.x + value * canvas_size.x, canvas_pos.y + canvas_size.y - curve_y * canvas_size.y);
		dl->AddCircleFilled(dot, 6.0f, IM_COL32(255, 200, 100, 255));

		ImGui::Dummy(canvas_size);
		ImGui::ProgressBar(value, ImVec2(-1, 0), "");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Cubic Bezier
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Cubic Bezier"))
	{
		ImGui::TextWrapped(
			"CSS-style cubic bezier curves. Control points (x1, y1) and (x2, y2) define "
			"the curve shape. Use cubic-bezier.com to design curves.");

		DocCodeSnippet(
			"iam_ease_desc ease = iam_ease_bezier(0.68f, -0.55f, 0.27f, 1.55f);\n"
			"// x1, y1, x2, y2 - control points");

		ImGui::Separator();

		static float x1 = 0.68f, y1 = -0.55f, x2 = 0.27f, y2 = 1.55f;
		ImGui::SliderFloat("x1", &x1, 0.0f, 1.0f);
		ImGui::SliderFloat("y1", &y1, -1.0f, 2.0f);
		ImGui::SliderFloat("x2", &x2, 0.0f, 1.0f);
		ImGui::SliderFloat("y2", &y2, -1.0f, 2.0f);

		static float target = 1.0f;
		if (ImGui::Button("Toggle##bezier")) target = (target > 0.5f) ? 0.0f : 1.0f;

		ImGuiID id = ImGui::GetID("bezier_demo");
		float value = iam_tween_float(id, DOC_CH_VALUE, target, 1.0f,
			iam_ease_bezier(x1, y1, x2, y2), iam_policy_crossfade, dt);

		ImGui::ProgressBar(value, ImVec2(-1, 0), "");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Spring Physics
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Spring Physics"))
	{
		ImGui::TextWrapped(
			"Physics-based spring animation with mass, stiffness, damping, and initial velocity. "
			"Creates natural overshooting motion.");

		DocCodeSnippet(
			"iam_ease_desc ease = iam_ease_spring_desc(\n"
			"    1.0f,    // mass\n"
			"    180.0f,  // stiffness (k)\n"
			"    12.0f,   // damping (c)\n"
			"    0.0f     // initial velocity\n"
			");");

		ImGui::Separator();

		static float mass = 1.0f, stiffness = 180.0f, damping = 12.0f, v0 = 0.0f;
		ImGui::SliderFloat("Mass", &mass, 0.1f, 5.0f);
		ImGui::SliderFloat("Stiffness", &stiffness, 10.0f, 500.0f);
		ImGui::SliderFloat("Damping", &damping, 1.0f, 50.0f);
		ImGui::SliderFloat("Initial Velocity", &v0, -10.0f, 10.0f);

		static float target = 1.0f;
		if (ImGui::Button("Toggle##spring")) target = (target > 0.5f) ? 0.0f : 1.0f;

		ImGuiID id = ImGui::GetID("spring_demo");
		float value = iam_tween_float(id, DOC_CH_VALUE, target, 1.0f,
			iam_ease_spring_desc(mass, stiffness, damping, v0), iam_policy_crossfade, dt);

		ImGui::ProgressBar(ImClamp(value, 0.0f, 1.0f), ImVec2(-1, 0), "");
		ImGui::Text("Value: %.3f (may overshoot)", value);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Steps
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Step Function"))
	{
		ImGui::TextWrapped(
			"Creates a stepped animation with discrete jumps. Modes control when the jump occurs: "
			"end (0), start (1), or both (2).");

		DocCodeSnippet(
			"iam_ease_desc ease = iam_ease_steps_desc(\n"
			"    5,   // step count\n"
			"    0    // mode: 0=end, 1=start, 2=both\n"
			");");

		ImGui::Separator();

		static int steps = 5;
		static int mode = 0;
		ImGui::SliderInt("Steps", &steps, 2, 20);
		char const* mode_names[] = { "Jump End (0)", "Jump Start (1)", "Jump Both (2)" };
		ImGui::Combo("Mode", &mode, mode_names, 3);

		static float target = 1.0f;
		if (ImGui::Button("Toggle##steps")) target = (target > 0.5f) ? 0.0f : 1.0f;

		ImGuiID id = ImGui::GetID("steps_demo");
		float value = iam_tween_float(id, DOC_CH_VALUE, target, 2.0f,
			iam_ease_steps_desc(steps, mode), iam_policy_crossfade, dt);

		ImGui::ProgressBar(value, ImVec2(-1, 0), "");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Custom Easing
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Custom Easing Functions"))
	{
		ImGui::TextWrapped(
			"Register your own easing function in one of 16 slots. The function receives t [0,1] "
			"and returns the eased value.");

		DocCodeSnippet(
			"// Define custom easing\n"
			"float my_ease(float t) { return t * t * (3.0f - 2.0f * t); }\n"
			"\n"
			"// Register in slot 0\n"
			"iam_register_custom_ease(0, my_ease);\n"
			"\n"
			"// Use it\n"
			"iam_ease_desc ease = iam_ease_custom_fn(0);");

		ImGui::Separator();
		ImGui::Text("Interactive Example (Smoothstep easing):");

		// Register custom smoothstep easing
		static bool custom_registered = false;
		if (!custom_registered) {
			iam_register_custom_ease(0, [](float t) -> float {
				return t * t * (3.0f - 2.0f * t);  // Smoothstep
			});
			custom_registered = true;
		}

		static float target = 1.0f;
		if (ImGui::Button("Toggle##custom")) target = (target > 0.5f) ? 0.0f : 1.0f;

		ImGuiID id = ImGui::GetID("custom_ease_demo");
		float value = iam_tween_float(id, DOC_CH_VALUE, target, 1.0f,
			iam_ease_custom_fn(0), iam_policy_crossfade, dt);

		// Draw easing curve comparison
		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(200.0f, 80.0f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw custom curve
		ImVec2 prev(canvas_pos.x, canvas_pos.y + canvas_size.y);
		for (int i = 1; i <= 50; i++) {
			float t = (float)i / 50.0f;
			float y = t * t * (3.0f - 2.0f * t);
			ImVec2 curr(canvas_pos.x + t * canvas_size.x, canvas_pos.y + canvas_size.y - y * canvas_size.y);
			dl->AddLine(prev, curr, IM_COL32(91, 194, 231, 255), 2.0f);
			prev = curr;
		}

		ImGui::Dummy(canvas_size);
		ImGui::ProgressBar(value, ImVec2(-1, 0), "Custom Smoothstep");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 4: COLOR SPACES
// ============================================================

static void DocSection_ColorSpaces()
{
	DocSectionHeader("COLOR SPACES",
		"Color space selection dramatically affects how colors blend. ImAnim supports 5 spaces, "
		"each with different characteristics.");

	float dt = GetDocDeltaTime();

	char const* space_names[] = { "sRGB", "Linear sRGB", "HSV", "OKLAB", "OKLCH" };
	int spaces[] = { iam_col_srgb, iam_col_srgb_linear, iam_col_hsv, iam_col_oklab, iam_col_oklch };

	// --------------------------------------------------------
	// Comparison Demo
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Color Space Comparison"))
	{
		ImGui::TextWrapped(
			"Watch how the same color transition looks different in each space. "
			"OKLAB/OKLCH are perceptually uniform and avoid the 'muddy middle' problem.");

		static ImVec4 color_a(1.0f, 0.0f, 0.0f, 1.0f);  // Red
		static ImVec4 color_b(0.0f, 0.0f, 1.0f, 1.0f);  // Blue
		static bool toggle = false;

		ImGui::ColorEdit4("Color A", &color_a.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::ColorEdit4("Color B", &color_b.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		if (ImGui::Button("Toggle##colorspace")) toggle = !toggle;

		ImVec4 target = toggle ? color_b : color_a;

		ImGui::Separator();

		for (int i = 0; i < 5; i++) {
			ImGuiID id = ImGui::GetID(space_names[i]);
			ImVec4 color = iam_tween_color(id, DOC_CH_COLOR, target, 1.0f,
				iam_ease_preset(iam_ease_linear), iam_policy_crossfade, spaces[i], dt);

			ImGui::ColorButton(space_names[i], color, 0, ImVec2(150, 25));
			ImGui::SameLine();
			ImGui::Text("%s", space_names[i]);
		}

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Individual Space Details with Examples
	// --------------------------------------------------------
	static ImVec4 demo_color_a(1.0f, 0.0f, 0.0f, 1.0f);  // Red
	static ImVec4 demo_color_b(0.0f, 1.0f, 0.0f, 1.0f);  // Green

	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_col_srgb"))
	{
		ImGui::TextWrapped(
			"Simple linear interpolation in gamma-corrected sRGB space. Fast but can produce "
			"muddy colors when blending saturated hues.");

		ImGui::Separator();
		static float t = 0.5f;
		ImGui::SliderFloat("Blend##srgb", &t, 0.0f, 1.0f);

		ImVec4 blended = ImVec4(
			demo_color_a.x + (demo_color_b.x - demo_color_a.x) * t,
			demo_color_a.y + (demo_color_b.y - demo_color_a.y) * t,
			demo_color_a.z + (demo_color_b.z - demo_color_a.z) * t,
			1.0f);
		ImGui::ColorButton("sRGB Result", blended, 0, ImVec2(150, 30));
		ImGui::SameLine();
		ImGui::Text("Red -> Green (notice brown middle)");

		ImGui::TreePop();
	}

	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_col_srgb_linear"))
	{
		ImGui::TextWrapped(
			"Converts to linear light, blends, converts back. More physically accurate for "
			"light mixing but still not perceptually uniform.");

		ImGui::Separator();
		static float t = 0.5f;
		ImGui::SliderFloat("Blend##linear", &t, 0.0f, 1.0f);

		// Simplified linear blend visualization
		float r = sqrtf(demo_color_a.x * demo_color_a.x * (1 - t) + demo_color_b.x * demo_color_b.x * t);
		float g = sqrtf(demo_color_a.y * demo_color_a.y * (1 - t) + demo_color_b.y * demo_color_b.y * t);
		float b = sqrtf(demo_color_a.z * demo_color_a.z * (1 - t) + demo_color_b.z * demo_color_b.z * t);
		ImGui::ColorButton("Linear Result", ImVec4(r, g, b, 1.0f), 0, ImVec2(150, 30));

		ImGui::TreePop();
	}

	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_col_hsv"))
	{
		ImGui::TextWrapped(
			"Blends in Hue-Saturation-Value space with shortest-arc hue interpolation. "
			"Good for hue shifts but can have saturation dips.");

		ImGui::Separator();
		static float t = 0.5f;
		ImGui::SliderFloat("Blend##hsv", &t, 0.0f, 1.0f);

		// HSV blend: hue goes 0 -> 120 degrees (red to green)
		float h = t * 0.333f;  // 0 to 1/3 (red to green in HSV)
		ImVec4 result;
		ImGui::ColorConvertHSVtoRGB(h, 1.0f, 1.0f, result.x, result.y, result.z);
		result.w = 1.0f;
		ImGui::ColorButton("HSV Result", result, 0, ImVec2(150, 30));
		ImGui::SameLine();
		ImGui::Text("Goes through yellow");

		ImGui::TreePop();
	}

	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_col_oklab"))
	{
		ImGui::TextWrapped(
			"RECOMMENDED. Perceptually uniform color space by Bjorn Ottosson. "
			"Produces smooth, natural transitions without muddy midpoints.");

		ImGui::Separator();
		static float target_t = 1.0f;
		static bool toggle = false;
		if (ImGui::Button("Animate##oklab")) {
			toggle = !toggle;
			target_t = toggle ? 1.0f : 0.0f;
		}

		ImGuiID id = ImGui::GetID("oklab_demo");
		ImVec4 color = iam_tween_color(id, DOC_CH_COLOR, toggle ? demo_color_b : demo_color_a, 1.0f,
			iam_ease_preset(iam_ease_linear), iam_policy_crossfade, iam_col_oklab, dt);

		ImGui::ColorButton("OKLAB Result", color, 0, ImVec2(150, 30));
		ImGui::SameLine();
		ImGui::Text("Perceptually smooth");

		ImGui::TreePop();
	}

	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_col_oklch"))
	{
		ImGui::TextWrapped(
			"Cylindrical form of OKLAB (Lightness, Chroma, Hue). Like HSV but perceptually "
			"uniform. Hue uses shortest-arc interpolation.");

		ImGui::Separator();
		static bool toggle = false;
		if (ImGui::Button("Animate##oklch")) toggle = !toggle;

		ImGuiID id = ImGui::GetID("oklch_demo");
		ImVec4 color = iam_tween_color(id, DOC_CH_COLOR, toggle ? demo_color_b : demo_color_a, 1.0f,
			iam_ease_preset(iam_ease_linear), iam_policy_crossfade, iam_col_oklch, dt);

		ImGui::ColorButton("OKLCH Result", color, 0, ImVec2(150, 30));
		ImGui::SameLine();
		ImGui::Text("Hue interpolation");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 5: CLIP SYSTEM
// ============================================================

static void DocSection_ClipSystem()
{
	DocSectionHeader("CLIP SYSTEM",
		"The Clip API allows authoring timeline-based animations with keyframes. "
		"Define clips once, play them multiple times with different instances.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Basic Clip Creation
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Creating Clips"))
	{
		ImGui::TextWrapped(
			"Clips are authored using a fluent API. Call begin(), add keyframes, configure "
			"options, then call end() to finalize.");

		DocCodeSnippet(
			"// Define clip once (usually at startup)\n"
			"iam_clip::begin(CLIP_ID)\n"
			"    .key_float(CH_ALPHA, 0.0f, 0.0f)           // t=0: alpha=0\n"
			"    .key_float(CH_ALPHA, 0.5f, 1.0f, iam_ease_out_cubic)  // t=0.5: alpha=1\n"
			"    .key_float(CH_ALPHA, 1.0f, 0.0f)           // t=1: alpha=0\n"
			"    .end();");

		ImGui::Separator();
		ImGui::Text("Interactive Example:");

		static ImGuiID const DOC_CLIP_CREATE_DEMO = ImHashStr("doc_clip_create_demo");
		static ImGuiID const DOC_CH_SIZE = ImHashStr("doc_ch_size");
		static bool create_clip_init = false;
		if (!create_clip_init) {
			iam_clip::begin(DOC_CLIP_CREATE_DEMO)
				.key_float(DOC_CH_SIZE, 0.0f, 10.0f, iam_ease_out_elastic)
				.key_float(DOC_CH_SIZE, 1.2f, 40.0f)
				.key_float(DOC_CH_SIZE, 2.0f, 20.0f, iam_ease_in_out_cubic)
				.end();
			create_clip_init = true;
		}

		static ImGuiID create_inst_id = ImHashStr("doc_create_inst");
		if (ImGui::Button("Play Created Clip")) {
			iam_play(DOC_CLIP_CREATE_DEMO, create_inst_id);
		}

		iam_instance inst = iam_get_instance(create_inst_id);
		float size = 20.0f;
		if (inst.valid()) {
			inst.get_float(DOC_CH_SIZE, &size);
		}

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + 200, canvas_pos.y + 80),
			IM_COL32(30, 30, 40, 255), 4.0f);
		dl->AddCircleFilled(ImVec2(canvas_pos.x + 100, canvas_pos.y + 40), size,
			IM_COL32(91, 194, 231, 255));
		ImGui::Dummy(ImVec2(200, 80));

		iam_show_debug_timeline(create_inst_id);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Playing Clips
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Playing Clips"))
	{
		ImGui::TextWrapped(
			"Use iam_play() to start a clip on an instance. Query values with get_float(), etc. "
			"Control playback with pause(), resume(), seek(), stop().");

		DocCodeSnippet(
			"// Play clip\n"
			"iam_instance inst = iam_play(CLIP_ID, instance_id);\n"
			"\n"
			"// Query animated value\n"
			"float alpha;\n"
			"if (inst.get_float(CH_ALPHA, &alpha)) {\n"
			"    // Use alpha...\n"
			"}\n"
			"\n"
			"// Control playback\n"
			"inst.pause();\n"
			"inst.resume();\n"
			"inst.seek(0.5f);  // Jump to 50%\n"
			"inst.stop();");

		ImGui::Separator();
		ImGui::Text("Interactive Example:");

		// Create clip if not exists
		static bool clip_created = false;
		if (!clip_created) {
			iam_clip::begin(DOC_CLIP_FLOAT)
				.key_float(DOC_CH_ALPHA, 0.0f, 0.0f, iam_ease_out_cubic)
				.key_float(DOC_CH_ALPHA, 0.8f, 1.0f, iam_ease_in_out_cubic)
				.key_float(DOC_CH_ALPHA, 1.8f, 1.0f, iam_ease_in_out_cubic)
				.key_float(DOC_CH_ALPHA, 2.5f, 0.0f, iam_ease_in_cubic)
				.end();
			clip_created = true;
		}

		static ImGuiID inst_id = ImHashStr("doc_basic_clip_inst");
		static bool playing = false;

		if (ImGui::Button(playing ? "Stop##basic" : "Play##basic")) {
			if (!playing) {
				iam_play(DOC_CLIP_FLOAT, inst_id);
				playing = true;
			} else {
				iam_get_instance(inst_id).stop();
				playing = false;
			}
		}

		iam_instance inst = iam_get_instance(inst_id);
		float alpha = 0.0f;
		if (inst.valid()) {
			if (!inst.is_playing()) playing = false;
			inst.get_float(DOC_CH_ALPHA, &alpha);
			ImGui::SameLine();
			ImGui::Text("Time: %.2f / %.2f", inst.time(), inst.duration());
		}

		ImGui::ProgressBar(alpha, ImVec2(-1, 20), "Alpha");
		iam_show_debug_timeline(inst_id);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Looping
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Looping"))
	{
		ImGui::TextWrapped(
			"Clips can loop infinitely or a set number of times. Direction controls playback: "
			"normal (forward), reverse (backward), or alternate (ping-pong).");

		DocCodeSnippet(
			"iam_clip::begin(CLIP_ID)\n"
			"    .key_float(...)\n"
			"    .set_loop(\n"
			"        true,              // enable looping\n"
			"        iam_dir_alternate, // ping-pong\n"
			"        -1                 // infinite (-1) or count\n"
			"    )\n"
			"    .end();");

		ImGui::Separator();

		static bool loop_clip_created = false;
		if (!loop_clip_created) {
			iam_clip::begin(DOC_CLIP_LOOP)
				.key_float(DOC_CH_VALUE, 0.0f, 0.0f, iam_ease_in_out_cubic)
				.key_float(DOC_CH_VALUE, 1.5f, 1.0f)
				.set_loop(true, iam_dir_alternate, -1)
				.end();
			loop_clip_created = true;
		}

		static ImGuiID loop_inst_id = ImHashStr("doc_loop_inst");
		static bool loop_playing = false;

		if (ImGui::Button(loop_playing ? "Stop##loop" : "Play##loop")) {
			if (!loop_playing) {
				iam_play(DOC_CLIP_LOOP, loop_inst_id);
				loop_playing = true;
			} else {
				iam_get_instance(loop_inst_id).stop();
				loop_playing = false;
			}
		}

		iam_instance inst = iam_get_instance(loop_inst_id);
		float value = 0.0f;
		if (inst.valid()) {
			inst.get_float(DOC_CH_VALUE, &value);
		}
		ImGui::ProgressBar(value, ImVec2(-1, 20), "Looping (alternate)");
		iam_show_debug_timeline(loop_inst_id);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Delayed Start
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Delayed Start"))
	{
		ImGui::TextWrapped(
			"Add a delay before a clip starts playing. Useful for sequencing animations "
			"or waiting for other events.");

		DocCodeSnippet(
			"iam_clip::begin(CLIP_ID)\n"
			"    .key_float(CH, 0.0f, 0.0f)\n"
			"    .key_float(CH, 1.0f, 100.0f)\n"
			"    .set_delay(0.5f)  // Wait 0.5s before starting\n"
			"    .end();");

		ImGui::Separator();

		static bool delay_clip_created = false;
		if (!delay_clip_created) {
			iam_clip::begin(DOC_CLIP_DELAY)
				.key_float(DOC_CH_VALUE, 0.0f, 0.0f, iam_ease_out_cubic)
				.key_float(DOC_CH_VALUE, 1.0f, 1.0f)
				.set_delay(1.0f)  // 1 second delay
				.end();
			delay_clip_created = true;
		}

		static ImGuiID delay_inst_id = ImHashStr("doc_delay_inst");

		if (ImGui::Button("Play (1s delay)##delay")) {
			iam_play(DOC_CLIP_DELAY, delay_inst_id);
		}

		iam_instance inst = iam_get_instance(delay_inst_id);
		float value = 0.0f;
		if (inst.valid()) {
			inst.get_float(DOC_CH_VALUE, &value);
		}

		ImGui::Text("Animation starts after 1 second delay:");
		ImGui::ProgressBar(value, ImVec2(-1, 20), "");
		iam_show_debug_timeline(delay_inst_id);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Timing Variation
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Timing Variation"))
	{
		ImGui::TextWrapped(
			"Vary timing parameters on each loop iteration. Great for organic feel - "
			"bouncing balls that slow down, or animations that speed up.");

		DocCodeSnippet(
			"iam_clip::begin(CLIP_ID)\n"
			"    .key_float(CH, 0.0f, 0.0f)\n"
			"    .key_float(CH, 1.0f, 100.0f)\n"
			"    .set_loop(true, iam_dir_normal, 5)\n"
			"    // Timing variations per loop:\n"
			"    .set_duration_var(iam_varf_mul(0.8f))   // 20% faster each loop\n"
			"    .set_delay_var(iam_varf_inc(0.1f))     // +0.1s gap each loop\n"
			"    .set_timescale_var(iam_varf_mul(1.1f)) // Speed up\n"
			"    .end();");

		ImGui::Separator();

		static bool timing_clip_created = false;
		if (!timing_clip_created) {
			iam_clip::begin(DOC_CLIP_TIMING_VAR)
				.key_float(DOC_CH_VALUE, 0.0f, 0.0f, iam_ease_out_bounce)
				.key_float(DOC_CH_VALUE, 1.0f, 1.0f)
				.set_loop(true, iam_dir_normal, 6)
				.set_duration_var(iam_varf_mul(0.85f))  // Gets faster
				.end();
			timing_clip_created = true;
		}

		static ImGuiID timing_inst_id = ImHashStr("doc_timing_inst");

		if (ImGui::Button("Play (duration decreases)##timing")) {
			iam_play(DOC_CLIP_TIMING_VAR, timing_inst_id);
		}

		iam_instance inst = iam_get_instance(timing_inst_id);
		float value = 0.0f;
		if (inst.valid()) {
			inst.get_float(DOC_CH_VALUE, &value);
		}

		ImGui::Text("Each loop is 15%% faster than the previous:");
		ImGui::ProgressBar(value, ImVec2(-1, 20), "");
		iam_show_debug_timeline(timing_inst_id);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Multi-Channel Clips
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Multi-Channel Clips"))
	{
		ImGui::TextWrapped(
			"A single clip can animate multiple channels simultaneously. Each channel has "
			"independent keyframes and timing.");

		DocCodeSnippet(
			"iam_clip::begin(CLIP_ID)\n"
			"    // Alpha channel\n"
			"    .key_float(CH_ALPHA, 0.0f, 0.0f)\n"
			"    .key_float(CH_ALPHA, 0.5f, 1.0f)\n"
			"    // Position channel\n"
			"    .key_vec2(CH_POS, 0.0f, ImVec2(0, 0))\n"
			"    .key_vec2(CH_POS, 1.0f, ImVec2(100, 50))\n"
			"    // Color channel\n"
			"    .key_color(CH_COLOR, 0.0f, red, iam_col_oklab)\n"
			"    .key_color(CH_COLOR, 1.0f, blue, iam_col_oklab)\n"
			"    .end();");

		ImGui::Separator();

		static bool multi_clip_created = false;
		if (!multi_clip_created) {
			iam_clip::begin(DOC_CLIP_VEC2)
				.key_float(DOC_CH_ALPHA, 0.0f, 0.0f, iam_ease_out_cubic)
				.key_float(DOC_CH_ALPHA, 1.0f, 1.0f)
				.key_float(DOC_CH_ALPHA, 2.5f, 1.0f)
				.key_float(DOC_CH_ALPHA, 3.5f, 0.0f, iam_ease_in_cubic)
				.key_vec2(DOC_CH_POS, 0.0f, ImVec2(20, 40), iam_ease_out_back)
				.key_vec2(DOC_CH_POS, 1.75f, ImVec2(180, 40))
				.key_vec2(DOC_CH_POS, 3.5f, ImVec2(100, 80), iam_ease_in_out_cubic)
				.key_color(DOC_CH_COLOR, 0.0f, ImVec4(1, 0, 0, 1), iam_col_oklab)
				.key_color(DOC_CH_COLOR, 1.75f, ImVec4(0, 1, 0, 1), iam_col_oklab)
				.key_color(DOC_CH_COLOR, 3.5f, ImVec4(0, 0, 1, 1), iam_col_oklab)
				.set_loop(true, iam_dir_normal, -1)
				.end();
			multi_clip_created = true;
		}

		static ImGuiID multi_inst_id = ImHashStr("doc_multi_inst");
		static bool multi_playing = false;

		if (ImGui::Button(multi_playing ? "Stop##multi" : "Play##multi")) {
			if (!multi_playing) {
				iam_play(DOC_CLIP_VEC2, multi_inst_id);
				multi_playing = true;
			} else {
				iam_get_instance(multi_inst_id).stop();
				multi_playing = false;
			}
		}

		float alpha = 1.0f;
		ImVec2 pos(100, 60);
		ImVec4 color(1, 0, 0, 1);  // Start red

		iam_instance inst = iam_get_instance(multi_inst_id);
		if (inst.valid()) {
			inst.get_float(DOC_CH_ALPHA, &alpha);
			inst.get_vec2(DOC_CH_POS, &pos);
			inst.get_color(DOC_CH_COLOR, &color, iam_col_oklab);
		}

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(220, 120);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, alpha));
		dl->AddCircleFilled(ImVec2(canvas_pos.x + pos.x, canvas_pos.y + pos.y), 15.0f, col);

		ImGui::Dummy(canvas_size);
		iam_show_debug_timeline(multi_inst_id);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Stagger
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Stagger"))
	{
		ImGui::TextWrapped(
			"Stagger automatically distributes delay across multiple instances. Perfect for "
			"list animations where items appear sequentially.");

		DocCodeSnippet(
			"iam_clip::begin(CLIP_ID)\n"
			"    .key_float(CH_ALPHA, 0.0f, 0.0f)\n"
			"    .key_float(CH_ALPHA, 0.3f, 1.0f)\n"
			"    .set_stagger(\n"
			"        10,    // item count\n"
			"        0.1f,  // delay per item\n"
			"        0.0f   // center bias (0=left-to-right)\n"
			"    )\n"
			"    .end();\n"
			"\n"
			"// Play with stagger index\n"
			"for (int i = 0; i < 10; i++) {\n"
			"    iam_play_stagger(CLIP_ID, inst_ids[i], i);\n"
			"}");

		ImGui::Separator();

		static bool stagger_clip_created = false;
		if (!stagger_clip_created) {
			iam_clip::begin(DOC_CLIP_STAGGER)
				.key_float(DOC_CH_ALPHA, 0.0f, 0.0f, iam_ease_out_cubic)
				.key_float(DOC_CH_ALPHA, 0.5f, 1.0f)
				.key_vec2(DOC_CH_POS, 0.0f, ImVec2(-30, 0), iam_ease_out_back)
				.key_vec2(DOC_CH_POS, 0.6f, ImVec2(0, 0))
				.set_stagger(6, 0.15f, 0.0f)
				.end();
			stagger_clip_created = true;
		}

		static ImGuiID stagger_inst_ids[6];
		static bool stagger_initialized = false;
		if (!stagger_initialized) {
			for (int i = 0; i < 6; i++)
				stagger_inst_ids[i] = ImHashStr("doc_stagger_inst") + i;
			stagger_initialized = true;
		}

		if (ImGui::Button("Play Stagger")) {
			for (int i = 0; i < 6; i++)
				iam_play_stagger(DOC_CLIP_STAGGER, stagger_inst_ids[i], i);
		}

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + 300, canvas_pos.y + 150),
			IM_COL32(30, 30, 40, 255), 4.0f);

		for (int i = 0; i < 6; i++) {
			iam_instance inst = iam_get_instance(stagger_inst_ids[i]);
			float alpha = 0.0f;
			ImVec2 offset(0, 0);
			if (inst.valid()) {
				inst.get_float(DOC_CH_ALPHA, &alpha);
				inst.get_vec2(DOC_CH_POS, &offset);
			}

			float y = canvas_pos.y + 15 + i * 22;
			ImU32 col = IM_COL32(91, 194, 231, (int)(alpha * 255));
			dl->AddRectFilled(
				ImVec2(canvas_pos.x + 10 + offset.x, y),
				ImVec2(canvas_pos.x + 280 + offset.x, y + 18),
				col, 4.0f);
		}

		ImGui::Dummy(ImVec2(300, 150));

		// Show timeline for first stagger instance
		iam_show_debug_timeline(stagger_inst_ids[0]);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Chaining
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Animation Chaining"))
	{
		ImGui::TextWrapped(
			"Chain clips together so one plays after another completes. Use then() on an "
			"instance to queue the next clip.");

		DocCodeSnippet(
			"// Each chained clip needs its own instance ID\n"
			"iam_instance inst_a = iam_play(CLIP_A, INST_A);\n"
			"inst_a.then(CLIP_B, INST_B);  // CLIP_B plays after A\n"
			"inst_a.then_delay(0.5f);      // Optional delay");

		ImGui::Separator();

		static bool chain_clips_created = false;
		if (!chain_clips_created) {
			// Clip A: move right (1.5s)
			iam_clip::begin(DOC_CLIP_CHAIN_A)
				.key_float(DOC_CH_X, 0.0f, 20.0f, iam_ease_out_cubic)
				.key_float(DOC_CH_X, 1.5f, 150.0f)
				.end();

			// Clip B: change color and scale (1.5s)
			iam_clip::begin(DOC_CLIP_CHAIN_B)
				.key_color(DOC_CH_COLOR, 0.0f, ImVec4(0.36f, 0.76f, 0.9f, 1.0f), iam_col_oklab, iam_ease_out_cubic)
				.key_color(DOC_CH_COLOR, 1.5f, ImVec4(0.9f, 0.3f, 0.2f, 1.0f), iam_col_oklab)
				.key_float(DOC_CH_SCALE, 0.0f, 1.0f, iam_ease_out_back)
				.key_float(DOC_CH_SCALE, 1.5f, 1.8f)
				.end();

			chain_clips_created = true;
		}

		static ImGuiID chain_inst_a = ImHashStr("doc_chain_inst_a");
		static ImGuiID chain_inst_b = ImHashStr("doc_chain_inst_b");

		if (ImGui::Button("Play Chain (A -> B)")) {
			// Each clip gets its own instance ID
			iam_instance inst = iam_play(DOC_CLIP_CHAIN_A, chain_inst_a);
			inst.then(DOC_CLIP_CHAIN_B, chain_inst_b);
		}

		// Default values
		float x = 20.0f, scale = 1.0f;
		ImVec4 color(0.36f, 0.76f, 0.9f, 1.0f);

		// Read from instance A (position)
		iam_instance inst_a = iam_get_instance(chain_inst_a);
		if (inst_a.valid()) {
			float tmp_x = x;
			if (inst_a.get_float(DOC_CH_X, &tmp_x)) x = tmp_x;
		}

		// Read from instance B (color, scale)
		iam_instance inst_b = iam_get_instance(chain_inst_b);
		if (inst_b.valid()) {
			float tmp_scale = scale;
			ImVec4 tmp_color = color;
			if (inst_b.get_float(DOC_CH_SCALE, &tmp_scale)) scale = tmp_scale;
			if (inst_b.get_color(DOC_CH_COLOR, &tmp_color, iam_col_oklab)) color = tmp_color;
		}

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + 300, canvas_pos.y + 80),
			IM_COL32(30, 30, 40, 255), 4.0f);

		float size = 20.0f * scale;
		ImU32 col = ImGui::ColorConvertFloat4ToU32(color);
		dl->AddCircleFilled(ImVec2(canvas_pos.x + x, canvas_pos.y + 40), size, col);

		ImGui::Dummy(ImVec2(300, 80));

		ImGui::Text("Clip A (position):");
		iam_show_debug_timeline(chain_inst_a);
		ImGui::Text("Clip B (color, scale):");
		iam_show_debug_timeline(chain_inst_b);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Markers
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Timeline Markers"))
	{
		ImGui::TextWrapped(
			"Markers trigger callbacks at specific times during playback. Useful for "
			"synchronizing sound effects, spawning particles, etc.");

		DocCodeSnippet(
			"void on_marker(ImGuiID inst, ImGuiID marker_id, float time, void* user) {\n"
			"    // Handle marker event\n"
			"}\n"
			"\n"
			"iam_clip::begin(CLIP_ID)\n"
			"    .key_float(...)\n"
			"    .marker(0.5f, MARKER_ID, on_marker, user_data)\n"
			"    .marker(0.8f, on_marker)  // Auto-generated ID\n"
			"    .end();");

		ImGui::Separator();
		ImGui::Text("Interactive Example:");

		static int marker_hit_count = 0;
		static float last_marker_time = 0.0f;

		static ImGuiID const DOC_CLIP_MARKER_DEMO = ImHashStr("doc_clip_marker_demo");
		static bool marker_clip_init = false;
		if (!marker_clip_init) {
			iam_clip::begin(DOC_CLIP_MARKER_DEMO)
				.key_float(DOC_CH_VALUE, 0.0f, 0.0f, iam_ease_out_cubic)
				.key_float(DOC_CH_VALUE, 2.0f, 1.0f)
				.marker(0.5f, [](ImGuiID, ImGuiID, float t, void* user) {
					*(int*)user += 1;
				}, &marker_hit_count)
				.marker(1.0f, [](ImGuiID, ImGuiID, float t, void* user) {
					*(int*)user += 1;
				}, &marker_hit_count)
				.marker(1.5f, [](ImGuiID, ImGuiID, float t, void* user) {
					*(int*)user += 1;
				}, &marker_hit_count)
				.end();
			marker_clip_init = true;
		}

		static ImGuiID marker_inst_id = ImHashStr("doc_marker_inst");
		if (ImGui::Button("Play (3 markers)##marker")) {
			marker_hit_count = 0;
			iam_play(DOC_CLIP_MARKER_DEMO, marker_inst_id);
		}

		ImGui::SameLine();
		ImGui::Text("Markers triggered: %d", marker_hit_count);

		iam_instance inst = iam_get_instance(marker_inst_id);
		float value = 0.0f;
		if (inst.valid()) {
			inst.get_float(DOC_CH_VALUE, &value);
		}
		ImGui::ProgressBar(value, ImVec2(-1, 20), "");
		iam_show_debug_timeline(marker_inst_id);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Variation
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Repeat Variation"))
	{
		ImGui::TextWrapped(
			"Variation modifies keyframe values on each loop iteration. Create dynamic "
			"animations where values increment, randomize, or follow patterns.");

		DocCodeSnippet(
			"// Value changes each loop\n"
			"iam_clip::begin(CLIP_ID)\n"
			"    .key_float_var(CH_X, 0.0f, 0.0f,\n"
			"        iam_varf_inc(10.0f))  // +10 each loop\n"
			"    .key_float_var(CH_X, 1.0f, 100.0f,\n"
			"        iam_varf_rand(20.0f)) // Random +/-20\n"
			"    .set_loop(true)\n"
			"    .end();\n"
			"\n"
			"// Variation types:\n"
			"iam_varf_inc(amount)     // Increment\n"
			"iam_varf_dec(amount)     // Decrement\n"
			"iam_varf_mul(factor)     // Multiply\n"
			"iam_varf_rand(range)     // Random +/- range\n"
			"iam_varf_pingpong(amt)   // Alternate +/-");

		ImGui::Separator();
		ImGui::Text("Interactive Example (Y increases each loop iteration):");

		static ImGuiID const DOC_CLIP_VAR_DEMO = ImHashStr("doc_clip_var_demo");
		static ImGuiID const DOC_CH_VAR_Y = ImHashStr("doc_ch_var_y");
		static bool var_clip_init = false;
		static int var_play_count = 0;
		if (!var_clip_init) {
			iam_clip::begin(DOC_CLIP_VAR_DEMO)
				.key_float(DOC_CH_X, 0.0f, 20.0f, iam_ease_out_cubic)
				.key_float(DOC_CH_X, 0.8f, 180.0f)
				.key_float_var(DOC_CH_VAR_Y, 0.0f, 20.0f, iam_varf_inc(15.0f))  // Y increases each loop
				.key_float_var(DOC_CH_VAR_Y, 0.8f, 20.0f, iam_varf_inc(15.0f))
				.set_loop(true, iam_dir_normal, 4)  // Loop 4 times (5 total iterations)
				.end();
			var_clip_init = true;
		}

		static ImGuiID var_inst_id = ImHashStr("doc_var_inst");
		if (ImGui::Button("Play Variation##var")) {
			iam_instance old_inst = iam_get_instance(var_inst_id);
			if (old_inst.valid())
				old_inst.destroy();
			iam_play(DOC_CLIP_VAR_DEMO, var_inst_id);
			var_play_count++;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset##var")) {
			iam_instance old_inst = iam_get_instance(var_inst_id);
			if (old_inst.valid())
				old_inst.destroy();
			var_play_count = 0;
		}

		iam_instance inst = iam_get_instance(var_inst_id);
		float x = 20.0f, y = 20.0f;
		int loop_iter = 0;
		bool is_playing = false;
		if (inst.valid()) {
			inst.get_float(DOC_CH_X, &x);
			inst.get_float(DOC_CH_VAR_Y, &y);
			is_playing = inst.is_playing();
			// Estimate loop iteration from Y value (clamped to valid range)
			loop_iter = ImClamp((int)((y - 20.0f) / 15.0f), 0, 4);
		}

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + 220, canvas_pos.y + 100),
			IM_COL32(30, 30, 40, 255), 4.0f);
		dl->AddCircleFilled(ImVec2(canvas_pos.x + x, canvas_pos.y + ImClamp(y, 20.0f, 85.0f)), 8.0f,
			IM_COL32(91, 194, 231, 255));
		ImGui::Dummy(ImVec2(220, 100));

		ImGui::Text("Y: %.0f | Loop: %d/5 | %s", y, loop_iter + 1, is_playing ? "Playing..." : "Done");
		iam_show_debug_timeline(var_inst_id);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Sequential/Parallel Groups
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Sequential & Parallel Groups"))
	{
		ImGui::TextWrapped(
			"Organize keyframes into groups. Sequential groups play one after another. "
			"Parallel groups start at the same time.");

		DocCodeSnippet(
			"iam_clip::begin(CLIP_ID)\n"
			"    // Sequential: B starts after A ends\n"
			"    .seq_begin()\n"
			"        .key_float(CH_A, 0.0f, 0.0f)\n"
			"        .key_float(CH_A, 0.5f, 1.0f)\n"
			"    .seq_end()\n"
			"    .seq_begin()\n"
			"        .key_float(CH_B, 0.0f, 0.0f)  // Starts at 0.5s\n"
			"        .key_float(CH_B, 0.5f, 1.0f)\n"
			"    .seq_end()\n"
			"\n"
			"    // Parallel: Both start together\n"
			"    .par_begin()\n"
			"        .key_float(CH_C, 0.0f, 0.0f)\n"
			"        .key_float(CH_D, 0.0f, 1.0f)\n"
			"    .par_end()\n"
			"    .end();");

		ImGui::Separator();
		ImGui::Text("Interactive Example (Sequential: A then B):");

		static ImGuiID const DOC_CLIP_SEQ_DEMO = ImHashStr("doc_clip_seq_demo");
		static ImGuiID const DOC_CH_SEQ_A = ImHashStr("doc_ch_seq_a");
		static ImGuiID const DOC_CH_SEQ_B = ImHashStr("doc_ch_seq_b");
		static bool seq_clip_init = false;
		if (!seq_clip_init) {
			iam_clip::begin(DOC_CLIP_SEQ_DEMO)
				// First: A moves right (0 to 1s)
				.key_float(DOC_CH_SEQ_A, 0.0f, 20.0f, iam_ease_out_cubic)
				.key_float(DOC_CH_SEQ_A, 1.0f, 150.0f)
				// Then: B moves right (starts at t=1s)
				.key_float(DOC_CH_SEQ_B, 1.0f, 20.0f, iam_ease_out_back)
				.key_float(DOC_CH_SEQ_B, 2.0f, 150.0f)
				.end();
			seq_clip_init = true;
		}

		static ImGuiID seq_inst_id = ImHashStr("doc_seq_inst");
		if (ImGui::Button("Play Sequential##seq")) {
			iam_play(DOC_CLIP_SEQ_DEMO, seq_inst_id);
		}

		iam_instance inst = iam_get_instance(seq_inst_id);
		float a_x = 20.0f, b_x = 20.0f;
		if (inst.valid()) {
			inst.get_float(DOC_CH_SEQ_A, &a_x);
			inst.get_float(DOC_CH_SEQ_B, &b_x);
		}

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + 200, canvas_pos.y + 70),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw A (cyan)
		dl->AddCircleFilled(ImVec2(canvas_pos.x + a_x, canvas_pos.y + 20), 10.0f,
			IM_COL32(91, 194, 231, 255));
		dl->AddText(ImVec2(canvas_pos.x + a_x - 3, canvas_pos.y + 14), IM_COL32_WHITE, "A");

		// Draw B (coral)
		dl->AddCircleFilled(ImVec2(canvas_pos.x + b_x, canvas_pos.y + 50), 10.0f,
			IM_COL32(204, 120, 88, 255));
		dl->AddText(ImVec2(canvas_pos.x + b_x - 3, canvas_pos.y + 44), IM_COL32_WHITE, "B");

		ImGui::Dummy(ImVec2(200, 70));

		iam_show_debug_timeline(seq_inst_id);

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 6: MOTION PATHS
// ============================================================

static void DocSection_MotionPaths()
{
	DocSectionHeader("MOTION PATHS",
		"Animate objects along curves. ImAnim supports linear, quadratic/cubic Bezier, "
		"and Catmull-Rom spline segments.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Path Creation
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Creating Paths"))
	{
		ImGui::TextWrapped(
			"Build paths using the fluent API. Start with begin(), add segments, "
			"optionally close(), then call end().");

		DocCodeSnippet(
			"// Create a path\n"
			"iam_path::begin(PATH_ID, ImVec2(0, 0))  // Start point\n"
			"    .line_to(ImVec2(100, 0))            // Linear segment\n"
			"    .quadratic_to(ctrl, end)            // Quadratic bezier\n"
			"    .cubic_to(ctrl1, ctrl2, end)        // Cubic bezier\n"
			"    .catmull_to(end, 0.5f)              // Catmull-Rom\n"
			"    .close()                            // Return to start\n"
			"    .end();");

		ImGui::Separator();
		ImGui::Text("Interactive Example (Triangle path):");

		static ImGuiID const DOC_PATH_CREATE_DEMO = ImHashStr("doc_path_create_demo");
		static bool create_path_init = false;
		if (!create_path_init) {
			iam_path::begin(DOC_PATH_CREATE_DEMO, ImVec2(100, 20))
				.line_to(ImVec2(180, 80))
				.line_to(ImVec2(20, 80))
				.close()
				.end();
			create_path_init = true;
		}

		static float path_t = 0.0f;
		path_t += dt * 0.3f;
		if (path_t > 1.0f) path_t -= 1.0f;

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + 200, canvas_pos.y + 100),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw path
		dl->AddTriangle(
			ImVec2(canvas_pos.x + 100, canvas_pos.y + 20),
			ImVec2(canvas_pos.x + 180, canvas_pos.y + 80),
			ImVec2(canvas_pos.x + 20, canvas_pos.y + 80),
			IM_COL32(100, 100, 120, 255), 2.0f);

		// Draw animated dot
		ImVec2 pos = iam_path_evaluate(DOC_PATH_CREATE_DEMO, path_t);
		dl->AddCircleFilled(ImVec2(canvas_pos.x + pos.x, canvas_pos.y + pos.y), 6.0f,
			IM_COL32(91, 194, 231, 255));

		ImGui::Dummy(ImVec2(200, 100));

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Path Evaluation
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Path Evaluation"))
	{
		ImGui::TextWrapped(
			"Sample position, tangent, or angle at any point along the path. "
			"Use arc-length parameterization for constant-speed animation.");

		DocCodeSnippet(
			"// Sample at parameter t [0,1]\n"
			"ImVec2 pos = iam_path_evaluate(PATH_ID, t);\n"
			"ImVec2 tangent = iam_path_tangent(PATH_ID, t);\n"
			"float angle = iam_path_angle(PATH_ID, t);\n"
			"\n"
			"// Arc-length (constant speed)\n"
			"iam_path_build_arc_lut(PATH_ID, 64);  // Build LUT\n"
			"ImVec2 pos = iam_path_evaluate_at_distance(PATH_ID, dist);");

		ImGui::Separator();
		ImGui::Text("Interactive Example (tangent & normal visualization):");

		static float eval_t = 0.5f;
		ImGui::SliderFloat("t##eval", &eval_t, 0.0f, 1.0f);

		// Reuse the path from the previous demo
		static ImGuiID const DOC_PATH_EVAL_DEMO = ImHashStr("doc_path_eval_demo");
		static bool eval_path_init = false;
		if (!eval_path_init) {
			iam_path::begin(DOC_PATH_EVAL_DEMO, ImVec2(20, 60))
				.cubic_to(ImVec2(60, 10), ImVec2(140, 90), ImVec2(180, 40))
				.end();
			eval_path_init = true;
		}

		ImVec2 pos = iam_path_evaluate(DOC_PATH_EVAL_DEMO, eval_t);
		ImVec2 tangent = iam_path_tangent(DOC_PATH_EVAL_DEMO, eval_t);
		float angle = iam_path_angle(DOC_PATH_EVAL_DEMO, eval_t);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + 200, canvas_pos.y + 100),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw curve
		ImVec2 prev = iam_path_evaluate(DOC_PATH_EVAL_DEMO, 0.0f);
		for (int i = 1; i <= 50; i++) {
			float t = (float)i / 50.0f;
			ImVec2 curr = iam_path_evaluate(DOC_PATH_EVAL_DEMO, t);
			dl->AddLine(
				ImVec2(canvas_pos.x + prev.x, canvas_pos.y + prev.y),
				ImVec2(canvas_pos.x + curr.x, canvas_pos.y + curr.y),
				IM_COL32(100, 100, 120, 255), 2.0f);
			prev = curr;
		}

		// Draw point, tangent arrow, and normal arrow
		ImVec2 p(canvas_pos.x + pos.x, canvas_pos.y + pos.y);
		dl->AddCircleFilled(p, 6.0f, IM_COL32(91, 194, 231, 255));

		// Tangent arrow (orange)
		float len = 30.0f;
		ImVec2 arrow_end(p.x + tangent.x * len, p.y + tangent.y * len);
		dl->AddLine(p, arrow_end, IM_COL32(204, 120, 88, 255), 2.0f);

		// Normal arrow (perpendicular to tangent, green)
		ImVec2 normal(-tangent.y, tangent.x);  // 90 degree rotation
		ImVec2 normal_end(p.x + normal.x * len, p.y + normal.y * len);
		dl->AddLine(p, normal_end, IM_COL32(120, 204, 88, 255), 2.0f);

		ImGui::Dummy(ImVec2(200, 100));
		ImGui::TextColored(ImVec4(0.8f, 0.47f, 0.34f, 1.0f), "Tangent");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.47f, 0.8f, 0.34f, 1.0f), "Normal");
		ImGui::SameLine();
		ImGui::Text("  Angle: %.1f deg", angle * 57.2957795f);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Path Tweens
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Animating Along Paths"))
	{
		ImGui::TextWrapped(
			"Use iam_tween_path() to animate position along a path with easing.");

		DocCodeSnippet(
			"ImVec2 pos = iam_tween_path(\n"
			"    id, channel_id,\n"
			"    PATH_ID,         // Path to follow\n"
			"    duration, ease, policy, dt\n"
			");\n"
			"\n"
			"// Also animate rotation to follow path\n"
			"float angle = iam_tween_path_angle(...);");

		ImGui::Separator();
		ImGui::Text("Interactive Example (weaving path with multiple crossings):");

		// Create demo path - a weaving ribbon pattern with multiple self-crossings
		static ImGuiID const DOC_PATH_KNOT = ImHashStr("doc_path_knot");
		static bool path_created = false;
		if (!path_created) {
			// Weaving pattern: goes left-to-right while crossing over itself 3 times
			// Like a ribbon weaving through pegs
			iam_path::begin(DOC_PATH_KNOT, ImVec2(20, 65))
				// First wave: go right and down
				.cubic_to(ImVec2(60, 20), ImVec2(100, 110), ImVec2(140, 65))
				// Second wave: continue right, cross back up
				.cubic_to(ImVec2(180, 20), ImVec2(220, 110), ImVec2(260, 65))
				// Turn around at right edge
				.cubic_to(ImVec2(290, 40), ImVec2(290, 90), ImVec2(260, 65))
				// Go back left, weaving through the first pass (crossing #1)
				.cubic_to(ImVec2(220, 30), ImVec2(180, 100), ImVec2(140, 65))
				// Continue left, crossing again (crossing #2)
				.cubic_to(ImVec2(100, 30), ImVec2(60, 100), ImVec2(30, 65))
				// Small loop at left and come back (crossing #3)
				.cubic_to(ImVec2(10, 40), ImVec2(10, 90), ImVec2(40, 85))
				// Final weave back to near start
				.cubic_to(ImVec2(70, 80), ImVec2(50, 50), ImVec2(20, 65))
				.end();
			iam_path_build_arc_lut(DOC_PATH_KNOT, 256);
			path_created = true;
		}

		static ImGuiID const DOC_CLIP_PATH_ANIM = ImHashStr("doc_clip_path_anim");
		static bool path_clip_init = false;
		if (!path_clip_init) {
			iam_clip::begin(DOC_CLIP_PATH_ANIM)
				.key_float(DOC_CH_VALUE, 0.0f, 0.0f, iam_ease_in_out_cubic)
				.key_float(DOC_CH_VALUE, 8.0f, 1.0f)  // 8 seconds for slower animation
				.end();
			path_clip_init = true;
		}

		static ImGuiID path_anim_inst = ImHashStr("doc_path_anim_inst");
		if (ImGui::Button("Animate Along Knot")) {
			iam_play(DOC_CLIP_PATH_ANIM, path_anim_inst);
		}

		float path_t = 0.0f;
		iam_instance path_inst = iam_get_instance(path_anim_inst);
		if (path_inst.valid()) {
			path_inst.get_float(DOC_CH_VALUE, &path_t);
		}
		ImVec2 pos = iam_path_evaluate(DOC_PATH_KNOT, path_t);
		float angle = iam_path_angle(DOC_PATH_KNOT, path_t);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(320, 130);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw path with gradient coloring to show direction
		int segments = 100;
		ImVec2 prev = iam_path_evaluate(DOC_PATH_KNOT, 0.0f);
		for (int i = 1; i <= segments; i++) {
			float t = (float)i / (float)segments;
			ImVec2 curr = iam_path_evaluate(DOC_PATH_KNOT, t);
			// Color gradient: start=dim, end=bright to show direction
			ImU8 alpha = (ImU8)(80 + 100 * t);
			dl->AddLine(
				ImVec2(canvas_pos.x + prev.x, canvas_pos.y + prev.y),
				ImVec2(canvas_pos.x + curr.x, canvas_pos.y + curr.y),
				IM_COL32(100, 100, 140, alpha), 2.0f);
			prev = curr;
		}

		// Draw animated dot with rotation indicator (arrow)
		ImVec2 p(canvas_pos.x + pos.x, canvas_pos.y + pos.y);
		dl->AddCircleFilled(p, 8.0f, IM_COL32(91, 194, 231, 255));
		// Draw direction arrow
		float arrow_len = 15.0f;
		float cos_a = cosf(angle);
		float sin_a = sinf(angle);
		ImVec2 arrow_tip(p.x + cos_a * arrow_len, p.y + sin_a * arrow_len);
		dl->AddLine(p, arrow_tip, IM_COL32(255, 200, 100, 255), 2.0f);

		ImGui::Dummy(canvas_size);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Path Morphing
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Path Morphing"))
	{
		ImGui::TextWrapped(
			"Smoothly blend between two different paths. Great for shape transitions.");

		DocCodeSnippet(
			"ImVec2 pos = iam_path_morph(\n"
			"    PATH_A, PATH_B,\n"
			"    t,       // Position along path [0,1]\n"
			"    blend,   // Morph blend [0,1]: 0=A, 1=B\n"
			"    opts     // iam_morph_opts\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example (circle to square morph):");

		// Create two paths: a circle and a square
		static ImGuiID const DOC_PATH_CIRCLE = ImHashStr("doc_path_circle");
		static ImGuiID const DOC_PATH_SQUARE = ImHashStr("doc_path_square");
		static bool morph_paths_created = false;
		if (!morph_paths_created) {
			// Circle approximation using bezier curves
			float r = 40.0f;
			float cx = 80.0f, cy = 60.0f;
			float k = 0.5522847498f * r;  // Magic number for circle approximation
			iam_path::begin(DOC_PATH_CIRCLE, ImVec2(cx + r, cy))
				.cubic_to(ImVec2(cx + r, cy + k), ImVec2(cx + k, cy + r), ImVec2(cx, cy + r))
				.cubic_to(ImVec2(cx - k, cy + r), ImVec2(cx - r, cy + k), ImVec2(cx - r, cy))
				.cubic_to(ImVec2(cx - r, cy - k), ImVec2(cx - k, cy - r), ImVec2(cx, cy - r))
				.cubic_to(ImVec2(cx + k, cy - r), ImVec2(cx + r, cy - k), ImVec2(cx + r, cy))
				.end();

			// Square path
			float s = 40.0f;
			iam_path::begin(DOC_PATH_SQUARE, ImVec2(cx + s, cy - s))
				.cubic_to(ImVec2(cx + s, cy - s), ImVec2(cx + s, cy + s), ImVec2(cx + s, cy + s))
				.cubic_to(ImVec2(cx + s, cy + s), ImVec2(cx - s, cy + s), ImVec2(cx - s, cy + s))
				.cubic_to(ImVec2(cx - s, cy + s), ImVec2(cx - s, cy - s), ImVec2(cx - s, cy - s))
				.cubic_to(ImVec2(cx - s, cy - s), ImVec2(cx + s, cy - s), ImVec2(cx + s, cy - s))
				.end();
			morph_paths_created = true;
		}

		static float morph_blend = 0.0f;
		static bool morph_animating = false;
		static float morph_dir = 1.0f;

		if (ImGui::Button("Morph Shape")) {
			morph_animating = true;
		}

		if (morph_animating) {
			float dt = GetDocDeltaTime();
			morph_blend += morph_dir * dt * 0.8f;
			if (morph_blend >= 1.0f) {
				morph_blend = 1.0f;
				morph_dir = -1.0f;
			} else if (morph_blend <= 0.0f) {
				morph_blend = 0.0f;
				morph_dir = 1.0f;
				morph_animating = false;
			}
		}

		ImGui::SameLine();
		ImGui::SliderFloat("Blend##morph", &morph_blend, 0.0f, 1.0f);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(160, 120);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw the morphed shape
		int segments = 80;
		iam_morph_opts opts;  // Use defaults
		ImVec2 prev = iam_path_morph(DOC_PATH_CIRCLE, DOC_PATH_SQUARE, 0.0f, morph_blend, opts);
		for (int i = 1; i <= segments; i++) {
			float t = (float)i / (float)segments;
			ImVec2 curr = iam_path_morph(DOC_PATH_CIRCLE, DOC_PATH_SQUARE, t, morph_blend, opts);
			dl->AddLine(
				ImVec2(canvas_pos.x + prev.x, canvas_pos.y + prev.y),
				ImVec2(canvas_pos.x + curr.x, canvas_pos.y + curr.y),
				IM_COL32(91, 194, 231, 255), 2.0f);
			prev = curr;
		}

		// Labels
		ImVec2 label_pos(canvas_pos.x + 5, canvas_pos.y + 5);
		dl->AddText(label_pos, IM_COL32(150, 150, 150, 255),
			morph_blend < 0.5f ? "Circle" : "Square");

		ImGui::Dummy(canvas_size);

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 7: PROCEDURAL ANIMATION
// ============================================================

static void DocSection_Procedural()
{
	DocSectionHeader("PROCEDURAL ANIMATION",
		"Generate continuous motion without keyframes. Oscillators for periodic motion, "
		"shake for impacts, wiggle for organic movement, noise for complex patterns.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Oscillators
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Oscillators"))
	{
		ImGui::TextWrapped(
			"Continuous periodic animation with four wave types: sine, triangle, sawtooth, square.");

		DocCodeSnippet(
			"float value = iam_oscillate(\n"
			"    id,\n"
			"    amplitude,   // Peak value\n"
			"    frequency,   // Hz (cycles per second)\n"
			"    wave_type,   // iam_wave_sine/triangle/sawtooth/square\n"
			"    phase,       // Phase offset [0,1]\n"
			"    dt\n"
			");");

		ImGui::Separator();

		static int wave_type = iam_wave_sine;
		static float amplitude = 50.0f;
		static float frequency = 1.0f;

		char const* wave_names[] = { "Sine", "Triangle", "Sawtooth", "Square" };
		ImGui::Combo("Wave Type", &wave_type, wave_names, 4);
		ImGui::SliderFloat("Amplitude", &amplitude, 10.0f, 100.0f);
		ImGui::SliderFloat("Frequency", &frequency, 0.1f, 5.0f);

		ImGuiID id = ImGui::GetID("oscillator_demo");
		float value = iam_oscillate(id, amplitude, frequency, wave_type, 0.0f, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(250, 100);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		float center_y = canvas_pos.y + canvas_size.y / 2;
		dl->AddLine(ImVec2(canvas_pos.x, center_y), ImVec2(canvas_pos.x + canvas_size.x, center_y),
			IM_COL32(60, 60, 80, 255), 1.0f);

		float dot_x = canvas_pos.x + canvas_size.x / 2;
		float dot_y = center_y - value;
		dl->AddCircleFilled(ImVec2(dot_x, dot_y), 10.0f, IM_COL32(91, 194, 231, 255));

		ImGui::Dummy(canvas_size);
		ImGui::Text("Value: %.2f", value);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Shake
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Shake"))
	{
		ImGui::TextWrapped(
			"Decaying random shake for impact feedback. Trigger with iam_trigger_shake().");

		DocCodeSnippet(
			"// Trigger shake\n"
			"if (hit) iam_trigger_shake(id);\n"
			"\n"
			"// Get shake offset\n"
			"ImVec2 offset = iam_shake_vec2(\n"
			"    id,\n"
			"    ImVec2(20, 20),  // intensity\n"
			"    30.0f,           // frequency (Hz)\n"
			"    0.5f,            // decay time\n"
			"    dt\n"
			");");

		ImGui::Separator();

		static float intensity = 20.0f;
		static float shake_freq = 30.0f;
		static float decay = 0.5f;

		ImGui::SliderFloat("Intensity", &intensity, 5.0f, 50.0f);
		ImGui::SliderFloat("Frequency", &shake_freq, 10.0f, 60.0f);
		ImGui::SliderFloat("Decay", &decay, 0.1f, 2.0f);

		ImGuiID id = ImGui::GetID("shake_demo");
		if (ImGui::Button("Trigger Shake!")) {
			iam_trigger_shake(id);
		}

		ImVec2 offset = iam_shake_vec2(id, ImVec2(intensity, intensity), shake_freq, decay, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(150, 80);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImVec2 rect_pos(canvas_pos.x + 50 + offset.x, canvas_pos.y + 20 + offset.y);
		dl->AddRectFilled(rect_pos, ImVec2(rect_pos.x + 50, rect_pos.y + 40),
			IM_COL32(204, 120, 88, 255), 4.0f);

		ImGui::Dummy(canvas_size);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Wiggle
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Wiggle"))
	{
		ImGui::TextWrapped(
			"Continuous smooth random movement. Unlike shake, it doesn't decay.");

		DocCodeSnippet(
			"ImVec2 offset = iam_wiggle_vec2(\n"
			"    id,\n"
			"    ImVec2(10, 10),  // amplitude\n"
			"    2.0f,            // frequency\n"
			"    dt\n"
			");");

		ImGui::Separator();

		ImGuiID id = ImGui::GetID("wiggle_demo");
		ImVec2 offset = iam_wiggle_vec2(id, ImVec2(15, 15), 2.0f, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		ImVec2 center(canvas_pos.x + 75 + offset.x, canvas_pos.y + 40 + offset.y);
		dl->AddCircleFilled(center, 20.0f, IM_COL32(91, 194, 231, 255));

		ImGui::Dummy(ImVec2(150, 80));

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Noise Channels
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Noise Channels"))
	{
		ImGui::TextWrapped(
			"Multi-octave noise for complex organic motion. Supports Perlin, Simplex, "
			"Value, and Worley noise types.");

		DocCodeSnippet(
			"iam_noise_opts opts;\n"
			"opts.type = iam_noise_simplex;\n"
			"opts.octaves = 4;\n"
			"opts.persistence = 0.5f;\n"
			"opts.lacunarity = 2.0f;\n"
			"\n"
			"float value = iam_noise_channel_float(\n"
			"    id,\n"
			"    frequency,\n"
			"    amplitude,\n"
			"    opts,\n"
			"    dt\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example (noise visualization):");

		static ImGuiID const DOC_NOISE_CHAN_X = ImHashStr("doc_noise_chan_x");
		static ImGuiID const DOC_NOISE_CHAN_Y = ImHashStr("doc_noise_chan_y");
		static int noise_type = 1;  // Default to simplex
		static int noise_octaves = 3;
		static float noise_freq = 1.5f;

		ImGui::SliderInt("Octaves##noise", &noise_octaves, 1, 6);
		ImGui::SliderFloat("Frequency##noise", &noise_freq, 0.5f, 5.0f);
		const char* noise_types[] = { "Perlin", "Simplex", "Value", "Worley" };
		ImGui::Combo("Type##noise", &noise_type, noise_types, 4);

		iam_noise_opts opts;
		opts.type = (iam_noise_type)noise_type;
		opts.octaves = noise_octaves;
		opts.persistence = 0.5f;
		opts.lacunarity = 2.0f;
		opts.seed = 12345;

		float noise_x = iam_noise_channel_float(DOC_NOISE_CHAN_X, noise_freq, 50.0f, opts, dt);
		opts.seed = 67890;  // Different seed for Y
		float noise_y = iam_noise_channel_float(DOC_NOISE_CHAN_Y, noise_freq, 30.0f, opts, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(200, 80);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw center cross for reference
		ImVec2 center(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
		dl->AddLine(ImVec2(center.x - 20, center.y), ImVec2(center.x + 20, center.y),
			IM_COL32(60, 60, 70, 255), 1.0f);
		dl->AddLine(ImVec2(center.x, center.y - 20), ImVec2(center.x, center.y + 20),
			IM_COL32(60, 60, 70, 255), 1.0f);

		// Draw noisy dot
		ImVec2 dot_pos(center.x + noise_x, center.y + noise_y);
		dl->AddCircleFilled(dot_pos, 8.0f, IM_COL32(91, 194, 231, 255));

		// Draw trail effect
		static ImVec2 trail[16] = {};
		static int trail_idx = 0;
		static float trail_timer = 0.0f;
		trail_timer += dt;
		if (trail_timer > 0.05f) {
			trail_timer = 0.0f;
			trail[trail_idx] = dot_pos;
			trail_idx = (trail_idx + 1) % 16;
		}
		for (int i = 0; i < 16; i++) {
			int idx = (trail_idx + i) % 16;
			if (trail[idx].x > 0) {
				float alpha = (float)i / 16.0f * 100.0f;
				dl->AddCircleFilled(trail[idx], 3.0f, IM_COL32(91, 194, 231, (ImU8)alpha));
			}
		}

		ImGui::Dummy(canvas_size);

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 8: TEXT ANIMATION
// ============================================================

static void DocSection_TextAnimation()
{
	DocSectionHeader("TEXT ANIMATION",
		"Animate text along paths or with per-character stagger effects.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Text Along Path
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Text Along Path"))
	{
		ImGui::TextWrapped(
			"Render text following a motion path curve. Each character is positioned "
			"and rotated to follow the path tangent.");

		DocCodeSnippet(
			"iam_text_path_opts opts;\n"
			"opts.origin = screen_pos;     // Screen offset\n"
			"opts.offset = 0.0f;           // Start offset along path\n"
			"opts.letter_spacing = 2.0f;\n"
			"opts.align = iam_text_align_center;\n"
			"opts.color = IM_COL32_WHITE;\n"
			"\n"
			"// Static text\n"
			"iam_text_path(PATH_ID, \"Hello\", opts);\n"
			"\n"
			"// Animated reveal\n"
			"iam_text_path_animated(PATH_ID, \"Hello\", progress, opts);");

		ImGui::Separator();
		ImGui::Text("Interactive Example:");

		static ImGuiID const DOC_PATH_TEXT_DEMO = ImHashStr("doc_path_text_demo");
		static bool text_path_init = false;
		if (!text_path_init) {
			iam_path::begin(DOC_PATH_TEXT_DEMO, ImVec2(20, 70))
				.cubic_to(ImVec2(80, 20), ImVec2(220, 20), ImVec2(280, 70))
				.end();
			iam_path_build_arc_lut(DOC_PATH_TEXT_DEMO, 64);
			text_path_init = true;
		}

		static float text_offset = 0.0f;
		static float text_progress = 1.0f;
		static bool animating = false;

		if (ImGui::Button("Animate Text##textpath")) {
			animating = true;
			text_progress = 0.0f;
		}
		ImGui::SameLine();
		ImGui::SliderFloat("Offset##textpath", &text_offset, -50.0f, 50.0f);

		if (animating) {
			text_progress += dt * 0.5f;
			if (text_progress >= 1.0f) {
				text_progress = 1.0f;
				animating = false;
			}
		}

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + 300, canvas_pos.y + 100),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw path
		ImVec2 prev = iam_path_evaluate(DOC_PATH_TEXT_DEMO, 0.0f);
		for (int i = 1; i <= 50; i++) {
			float t = (float)i / 50.0f;
			ImVec2 curr = iam_path_evaluate(DOC_PATH_TEXT_DEMO, t);
			dl->AddLine(
				ImVec2(canvas_pos.x + prev.x, canvas_pos.y + prev.y),
				ImVec2(canvas_pos.x + curr.x, canvas_pos.y + curr.y),
				IM_COL32(60, 60, 80, 255), 1.0f);
			prev = curr;
		}

		// Draw text along path
		iam_text_path_opts opts;
		opts.origin = canvas_pos;
		opts.offset = text_offset;
		opts.letter_spacing = 2.0f;
		opts.align = iam_text_align_center;
		opts.color = IM_COL32(91, 194, 231, 255);

		iam_text_path_animated(DOC_PATH_TEXT_DEMO, "ImAnim Text Path!", text_progress, opts);

		ImGui::Dummy(ImVec2(300, 100));

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Text Stagger
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Text Stagger"))
	{
		ImGui::TextWrapped(
			"Per-character animation with staggered timing. Multiple effects available: "
			"fade, scale, slide, rotate, bounce, wave, typewriter.");

		DocCodeSnippet(
			"iam_text_stagger_opts opts;\n"
			"opts.pos = ImVec2(100, 100);\n"
			"opts.effect = iam_text_fx_bounce;\n"
			"opts.char_delay = 0.05f;    // Delay between chars\n"
			"opts.char_duration = 0.3f;  // Per-char animation time\n"
			"opts.effect_intensity = 20.0f;\n"
			"opts.color = IM_COL32_WHITE;\n"
			"\n"
			"iam_text_stagger(id, \"Hello!\", progress, opts);");

		ImGui::Separator();

		static int effect = iam_text_fx_bounce;
		static float progress = 0.0f;
		static bool playing = false;

		char const* effect_names[] = {
			"None", "Fade", "Scale", "Slide Up", "Slide Down",
			"Slide Left", "Slide Right", "Rotate", "Bounce", "Wave", "Typewriter"
		};
		ImGui::Combo("Effect", &effect, effect_names, IM_ARRAYSIZE(effect_names));

		if (ImGui::Button(playing ? "Reset##stagger" : "Play##stagger")) {
			playing = !playing;
			if (playing) progress = 0.0f;
		}

		if (playing) {
			progress += dt * 0.5f;
			if (progress > 1.0f) playing = false;
		}

		ImGui::ProgressBar(progress, ImVec2(200, 0), "");

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + 300, canvas_pos.y + 60),
			IM_COL32(30, 30, 40, 255), 4.0f);

		iam_text_stagger_opts opts;
		opts.pos = ImVec2(canvas_pos.x + 20, canvas_pos.y + 20);
		opts.effect = effect;
		opts.char_delay = 0.08f;
		opts.char_duration = 0.4f;
		opts.effect_intensity = 25.0f;
		opts.color = IM_COL32(91, 194, 231, 255);

		iam_text_stagger(ImGui::GetID("stagger_text_demo"), "Hello, ImAnim!", progress, opts);

		ImGui::Dummy(ImVec2(300, 60));

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 9: ADVANCED FEATURES
// ============================================================

static void DocSection_Advanced()
{
	DocSectionHeader("ADVANCED FEATURES",
		"Style interpolation, gradient animation, transform tweening, and more.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Style Interpolation
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Style Interpolation"))
	{
		ImGui::TextWrapped(
			"Smoothly transition between ImGui themes by interpolating all style properties.");

		DocCodeSnippet(
			"// Register styles\n"
			"iam_style_register(STYLE_DARK, dark_style);\n"
			"iam_style_register(STYLE_LIGHT, light_style);\n"
			"\n"
			"// Animated transition\n"
			"iam_style_tween(id, STYLE_LIGHT, 0.5f, ease, iam_col_oklab, dt);\n"
			"\n"
			"// Or manual blend\n"
			"iam_style_blend(STYLE_DARK, STYLE_LIGHT, t, iam_col_oklab);");

		ImGui::Separator();
		ImGui::Text("Interactive Example (color blend preview):");

		static float style_blend = 0.0f;
		static bool style_animating = false;
		static float style_dir = 1.0f;

		if (ImGui::Button("Animate Style Blend")) {
			style_animating = true;
		}
		ImGui::SameLine();
		ImGui::SliderFloat("Blend##style", &style_blend, 0.0f, 1.0f);

		if (style_animating) {
			style_blend += style_dir * dt * 1.0f;
			if (style_blend >= 1.0f) { style_blend = 1.0f; style_dir = -1.0f; }
			else if (style_blend <= 0.0f) { style_blend = 0.0f; style_dir = 1.0f; style_animating = false; }
		}

		// Preview style colors (dark to light)
		ImVec4 dark_bg(0.1f, 0.1f, 0.12f, 1.0f);
		ImVec4 light_bg(0.95f, 0.95f, 0.95f, 1.0f);
		ImVec4 dark_btn(0.2f, 0.4f, 0.8f, 1.0f);
		ImVec4 light_btn(0.3f, 0.6f, 0.95f, 1.0f);

		// Lerp colors
		ImVec4 bg_col(
			dark_bg.x + (light_bg.x - dark_bg.x) * style_blend,
			dark_bg.y + (light_bg.y - dark_bg.y) * style_blend,
			dark_bg.z + (light_bg.z - dark_bg.z) * style_blend, 1.0f);
		ImVec4 btn_col(
			dark_btn.x + (light_btn.x - dark_btn.x) * style_blend,
			dark_btn.y + (light_btn.y - dark_btn.y) * style_blend,
			dark_btn.z + (light_btn.z - dark_btn.z) * style_blend, 1.0f);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(180, 60);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			ImGui::ColorConvertFloat4ToU32(bg_col), 4.0f);
		// Fake button
		ImVec2 btn_pos(canvas_pos.x + 20, canvas_pos.y + 15);
		ImVec2 btn_size(80, 30);
		dl->AddRectFilled(btn_pos, ImVec2(btn_pos.x + btn_size.x, btn_pos.y + btn_size.y),
			ImGui::ColorConvertFloat4ToU32(btn_col), 4.0f);
		ImVec4 text_col = style_blend < 0.5f ? ImVec4(1,1,1,1) : ImVec4(0.1f,0.1f,0.1f,1);
		dl->AddText(ImVec2(btn_pos.x + 15, btn_pos.y + 7),
			ImGui::ColorConvertFloat4ToU32(text_col), "Button");

		ImGui::Dummy(canvas_size);
		ImGui::Text("%s", style_blend < 0.5f ? "Dark Theme" : "Light Theme");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Gradient Animation
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Gradient Animation"))
	{
		ImGui::TextWrapped(
			"Animate between color gradients with proper color space interpolation.");

		DocCodeSnippet(
			"// Create gradients\n"
			"iam_gradient grad_a;\n"
			"grad_a.add(0.0f, red).add(1.0f, yellow);\n"
			"\n"
			"iam_gradient grad_b;\n"
			"grad_b.add(0.0f, blue).add(0.5f, cyan).add(1.0f, green);\n"
			"\n"
			"// Animate\n"
			"iam_gradient result = iam_tween_gradient(\n"
			"    id, channel_id,\n"
			"    target_gradient,\n"
			"    duration, ease, policy, color_space, dt\n"
			");\n"
			"\n"
			"// Sample result\n"
			"ImVec4 color = result.sample(0.5f);");

		ImGui::Separator();
		ImGui::Text("Interactive Example (gradient blend):");

		static float grad_blend = 0.0f;
		static bool grad_animating = false;
		static float grad_dir = 1.0f;

		if (ImGui::Button("Animate Gradient")) {
			grad_animating = true;
		}
		ImGui::SameLine();
		ImGui::SliderFloat("Blend##grad", &grad_blend, 0.0f, 1.0f);

		if (grad_animating) {
			grad_blend += grad_dir * dt * 0.8f;
			if (grad_blend >= 1.0f) { grad_blend = 1.0f; grad_dir = -1.0f; }
			else if (grad_blend <= 0.0f) { grad_blend = 0.0f; grad_dir = 1.0f; grad_animating = false; }
		}

		// Gradient A: Red -> Yellow
		// Gradient B: Blue -> Cyan -> Green (smooth transition)
		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(200, 30);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		int steps = 50;
		float step_w = canvas_size.x / (float)steps;
		for (int i = 0; i < steps; i++) {
			float t = (float)i / (float)(steps - 1);
			// Gradient A: Red to Yellow
			ImVec4 col_a(1.0f, t, 0.0f, 1.0f);
			// Gradient B: Blue -> Cyan -> Green (continuous)
			// At t=0: Blue (0, 0, 1)
			// At t=0.5: Cyan (0, 1, 1)
			// At t=1: Green (0, 1, 0)
			ImVec4 col_b;
			if (t < 0.5f) {
				float lt = t * 2.0f;  // 0 to 1 over first half
				col_b = ImVec4(0.0f, lt, 1.0f, 1.0f);  // Blue to Cyan
			} else {
				float lt = (t - 0.5f) * 2.0f;  // 0 to 1 over second half
				col_b = ImVec4(0.0f, 1.0f, 1.0f - lt, 1.0f);  // Cyan to Green
			}
			// Blend
			ImVec4 col(
				col_a.x + (col_b.x - col_a.x) * grad_blend,
				col_a.y + (col_b.y - col_a.y) * grad_blend,
				col_a.z + (col_b.z - col_a.z) * grad_blend, 1.0f);
			ImVec2 p0(canvas_pos.x + i * step_w, canvas_pos.y);
			ImVec2 p1(canvas_pos.x + (i + 1) * step_w + 1, canvas_pos.y + canvas_size.y);
			dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(col));
		}

		ImGui::Dummy(canvas_size);
		ImGui::Text("%s", grad_blend < 0.5f ? "Red-Yellow" : "Blue-Cyan-Green");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Transform Animation
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Transform Animation"))
	{
		ImGui::TextWrapped(
			"Animate 2D transforms (position, rotation, scale) with proper rotation interpolation.");

		DocCodeSnippet(
			"iam_transform target;\n"
			"target.position = ImVec2(100, 50);\n"
			"target.rotation = 3.14f;  // radians\n"
			"target.scale = ImVec2(2.0f, 2.0f);\n"
			"\n"
			"iam_transform current = iam_tween_transform(\n"
			"    id, channel_id,\n"
			"    target,\n"
			"    duration, ease, policy,\n"
			"    iam_rotation_shortest,  // Rotation mode\n"
			"    dt\n"
			");\n"
			"\n"
			"// Apply to point\n"
			"ImVec2 transformed = current.apply(point);");

		ImGui::Separator();
		ImGui::Text("Interactive Example (rotation + scale):");

		static float trans_t = 0.0f;
		static bool trans_animating = false;
		static float trans_dir = 1.0f;

		if (ImGui::Button("Animate Transform")) {
			trans_animating = true;
		}
		ImGui::SameLine();
		ImGui::SliderFloat("t##trans", &trans_t, 0.0f, 1.0f);

		if (trans_animating) {
			trans_t += trans_dir * dt * 0.6f;
			if (trans_t >= 1.0f) { trans_t = 1.0f; trans_dir = -1.0f; }
			else if (trans_t <= 0.0f) { trans_t = 0.0f; trans_dir = 1.0f; trans_animating = false; }
		}

		// Smooth easing
		float ease_t = trans_t * trans_t * (3.0f - 2.0f * trans_t);

		float rotation = ease_t * 3.14159f;  // 0 to 180 degrees
		float scale = 1.0f + ease_t * 0.5f;  // 1.0 to 1.5

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(150, 80);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw rotating/scaling square
		ImVec2 center(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
		float half_size = 20.0f * scale;
		float cos_r = cosf(rotation);
		float sin_r = sinf(rotation);

		// Square corners
		ImVec2 corners[4] = {
			ImVec2(-half_size, -half_size),
			ImVec2(half_size, -half_size),
			ImVec2(half_size, half_size),
			ImVec2(-half_size, half_size)
		};

		// Rotate and translate
		for (int i = 0; i < 4; i++) {
			float x = corners[i].x * cos_r - corners[i].y * sin_r;
			float y = corners[i].x * sin_r + corners[i].y * cos_r;
			corners[i] = ImVec2(center.x + x, center.y + y);
		}

		dl->AddQuadFilled(corners[0], corners[1], corners[2], corners[3],
			IM_COL32(91, 194, 231, 200));
		dl->AddQuad(corners[0], corners[1], corners[2], corners[3],
			IM_COL32(120, 220, 255, 255), 2.0f);

		ImGui::Dummy(canvas_size);
		ImGui::Text("Rot: %.0f deg  Scale: %.2f", rotation * 57.2957795f, scale);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Rotation Modes
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Rotation Modes"))
	{
		ImGui::TextWrapped(
			"Control how rotation angles are interpolated. Different modes handle the "
			"wrap-around at 360 degrees differently, letting you control which direction "
			"the rotation takes.");

		DocCodeSnippet(
			"// Available rotation modes:\n"
			"iam_rotation_shortest  // Never rotates more than 180 deg (default)\n"
			"iam_rotation_longest   // Always takes the long way (>180 deg)\n"
			"iam_rotation_cw        // Always rotates clockwise\n"
			"iam_rotation_ccw       // Always rotates counter-clockwise\n"
			"iam_rotation_direct    // Raw lerp without unwrapping\n"
			"\n"
			"// Use with iam_tween_transform:\n"
			"iam_transform current = iam_tween_transform(\n"
			"    id, channel_id,\n"
			"    target,\n"
			"    duration, ease, policy,\n"
			"    iam_rotation_shortest,  // Rotation mode\n"
			"    dt\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example (try different modes):");

		static int rot_mode = iam_rotation_shortest;
		static float rot_target = 0.0f;

		ImGui::Text("Mode:");
		ImGui::RadioButton("Shortest##rotmode", &rot_mode, iam_rotation_shortest);
		ImGui::SameLine();
		ImGui::RadioButton("Longest##rotmode", &rot_mode, iam_rotation_longest);
		ImGui::SameLine();
		ImGui::RadioButton("CW##rotmode", &rot_mode, iam_rotation_cw);
		ImGui::RadioButton("CCW##rotmode", &rot_mode, iam_rotation_ccw);
		ImGui::SameLine();
		ImGui::RadioButton("Direct##rotmode", &rot_mode, iam_rotation_direct);

		ImGui::Text("Target Angle:");
		if (ImGui::Button("0##rot")) rot_target = 0.0f;
		ImGui::SameLine();
		if (ImGui::Button("90##rot")) rot_target = 1.5708f;
		ImGui::SameLine();
		if (ImGui::Button("180##rot")) rot_target = 3.14159f;
		ImGui::SameLine();
		if (ImGui::Button("270##rot")) rot_target = 4.7124f;
		ImGui::SameLine();
		if (ImGui::Button("360##rot")) rot_target = 6.28318f;

		iam_transform rot_target_tf;
		rot_target_tf.position = ImVec2(100.0f, 50.0f);
		rot_target_tf.rotation = rot_target;
		rot_target_tf.scale = ImVec2(1.0f, 1.0f);

		iam_transform rot_current = iam_tween_transform(
			ImGui::GetID("rot_mode_doc_demo"),
			ImGui::GetID("ch_rot"),
			rot_target_tf,
			1.0f,
			iam_ease_preset(iam_ease_out_cubic),
			iam_policy_crossfade,
			rot_mode,
			dt
		);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(200.0f, 100.0f);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw rotating arrow
		ImVec2 center(canvas_pos.x + rot_current.position.x, canvas_pos.y + rot_current.position.y);
		float arrow_len = 35.0f;
		float cos_r = cosf(rot_current.rotation);
		float sin_r = sinf(rot_current.rotation);
		ImVec2 arrow_end(center.x + arrow_len * cos_r, center.y + arrow_len * sin_r);

		// Arrow body
		dl->AddLine(center, arrow_end, IM_COL32(91, 194, 231, 255), 3.0f);
		dl->AddCircleFilled(center, 6.0f, IM_COL32(91, 194, 231, 255));
		dl->AddCircleFilled(arrow_end, 5.0f, IM_COL32(255, 200, 100, 255));

		// Draw target direction (faded)
		ImVec2 target_end(center.x + arrow_len * cosf(rot_target), center.y + arrow_len * sinf(rot_target));
		dl->AddLine(center, target_end, IM_COL32(255, 100, 100, 100), 1.5f);

		ImGui::Dummy(canvas_size);

		float deg = rot_current.rotation * 57.2958f;
		float target_deg = rot_target * 57.2958f;
		ImGui::Text("Current: %.0f deg -> Target: %.0f deg", deg, target_deg);

		ImGui::TextDisabled("Shortest: min rotation (<180)");
		ImGui::TextDisabled("Longest: max rotation (>180)");
		ImGui::TextDisabled("CW/CCW: forced direction");
		ImGui::TextDisabled("Direct: raw lerp (can spin multiple times)");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Relative Tweens
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Relative Tweens (Resize-Friendly)"))
	{
		ImGui::TextWrapped(
			"Animate values relative to window/viewport size. Animations survive resizes.");

		DocCodeSnippet(
			"// Position as % of window + pixel offset\n"
			"ImVec2 pos = iam_tween_vec2_rel(\n"
			"    id, channel_id,\n"
			"    ImVec2(0.5f, 0.5f),    // 50% of anchor\n"
			"    ImVec2(0, -20),        // -20px Y offset\n"
			"    duration, ease, policy,\n"
			"    iam_anchor_window,     // Anchor space\n"
			"    dt\n"
			");\n"
			"\n"
			"// Anchor spaces:\n"
			"iam_anchor_window_content  // GetContentRegionAvail()\n"
			"iam_anchor_window          // GetWindowSize()\n"
			"iam_anchor_viewport        // GetWindowViewport()->Size\n"
			"iam_anchor_last_item       // GetItemRectSize()");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Resolved Tweens
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Resolved Tweens (Dynamic Targets)"))
	{
		ImGui::TextWrapped(
			"Use callbacks to compute targets dynamically each frame. The target is "
			"resolved every frame, allowing animations to chase moving targets.");

		DocCodeSnippet(
			"float resolve_target(void* user) {\n"
			"    return some_dynamic_value();\n"
			"}\n"
			"\n"
			"float value = iam_tween_float_resolved(\n"
			"    id, channel_id,\n"
			"    resolve_target, user_data,\n"
			"    duration, ease, policy, dt\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example (chasing mouse position):");

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(280, 100);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// The "target" is the mouse position (clamped to canvas)
		ImVec2 mouse = ImGui::GetMousePos();
		float target_x = ImClamp(mouse.x - canvas_pos.x, 20.0f, canvas_size.x - 20.0f);
		float target_y = ImClamp(mouse.y - canvas_pos.y, 20.0f, canvas_size.y - 20.0f);

		// Store target in static for the resolver to access
		static ImVec2 s_resolved_target(140.0f, 50.0f);
		s_resolved_target = ImVec2(target_x, target_y);

		// Animate position chasing the dynamic target
		ImGuiID id = ImGui::GetID("resolved_demo");
		ImVec2 pos = iam_tween_vec2(id, ImHashStr("pos"), s_resolved_target, 0.3f,
			iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt);

		// Draw target crosshair
		dl->AddLine(ImVec2(canvas_pos.x + target_x - 8, canvas_pos.y + target_y),
			ImVec2(canvas_pos.x + target_x + 8, canvas_pos.y + target_y),
			IM_COL32(255, 100, 100, 150), 1.5f);
		dl->AddLine(ImVec2(canvas_pos.x + target_x, canvas_pos.y + target_y - 8),
			ImVec2(canvas_pos.x + target_x, canvas_pos.y + target_y + 8),
			IM_COL32(255, 100, 100, 150), 1.5f);

		// Draw chasing circle
		dl->AddCircleFilled(ImVec2(canvas_pos.x + pos.x, canvas_pos.y + pos.y), 12.0f,
			IM_COL32(91, 194, 231, 255));

		ImGui::Dummy(canvas_size);
		ImGui::TextDisabled("Move mouse over canvas - circle chases the target");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Rebase
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Rebase (Redirect In-Progress Animation)"))
	{
		ImGui::TextWrapped(
			"Change the target of an animation without restarting from the beginning. "
			"The animation smoothly redirects to the new target from its current position.");

		DocCodeSnippet(
			"// Animation is running toward target A...\n"
			"\n"
			"// Smoothly redirect to target B\n"
			"iam_rebase_float(id, channel_id, new_target, dt);");

		ImGui::Separator();
		ImGui::Text("Interactive Example (click buttons to redirect):");

		static float rebase_target = 50.0f;
		static int target_idx = 0;
		float targets[] = { 50.0f, 150.0f, 250.0f };

		if (ImGui::Button("Left##rebase")) { rebase_target = targets[0]; target_idx = 0; }
		ImGui::SameLine();
		if (ImGui::Button("Center##rebase")) { rebase_target = targets[1]; target_idx = 1; }
		ImGui::SameLine();
		if (ImGui::Button("Right##rebase")) { rebase_target = targets[2]; target_idx = 2; }

		ImGuiID id = ImGui::GetID("rebase_demo");
		float pos_x = iam_tween_float(id, ImHashStr("x"), rebase_target, 1.5f,
			iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(300, 50);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw target markers
		for (int i = 0; i < 3; i++) {
			ImU32 col = (i == target_idx) ? IM_COL32(255, 200, 100, 255) : IM_COL32(100, 100, 100, 150);
			dl->AddCircle(ImVec2(canvas_pos.x + targets[i], canvas_pos.y + 25), 8.0f, col, 0, 2.0f);
		}

		// Draw animated circle
		dl->AddCircleFilled(ImVec2(canvas_pos.x + pos_x, canvas_pos.y + 25), 12.0f,
			IM_COL32(91, 194, 231, 255));

		ImGui::Dummy(canvas_size);
		ImGui::TextDisabled("Click a target while animation is running to redirect");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Drag Feedback
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Drag Feedback"))
	{
		ImGui::TextWrapped(
			"Animated feedback for drag operations with snap-to-grid and overshoot. "
			"Provides smooth visual response during and after dragging.");

		DocCodeSnippet(
			"iam_drag_opts opts;\n"
			"opts.snap_grid = ImVec2(50, 50);  // Grid snapping\n"
			"opts.snap_duration = 0.2f;\n"
			"opts.overshoot = 0.3f;\n"
			"opts.ease_type = iam_ease_out_back;\n"
			"\n"
			"// During drag\n"
			"if (ImGui::IsItemActive()) {\n"
			"    feedback = iam_drag_update(id, mouse_pos, dt);\n"
			"} else if (was_active) {\n"
			"    feedback = iam_drag_release(id, mouse_pos, opts, dt);\n"
			"}\n"
			"\n"
			"// Use feedback.position for rendering");

		ImGui::Separator();
		ImGui::Text("Interactive Example (drag the box, release to snap):");

		// Draggable box state
		static ImVec2 box_pos(75.0f, 60.0f);
		static ImVec2 drag_offset(0, 0);
		static bool dragging = false;
		static ImVec2 snap_target(75.0f, 60.0f);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(300, 120);

		// Create an invisible button to capture mouse input and prevent window dragging
		ImGui::InvisibleButton("drag_canvas", canvas_size);
		bool canvas_hovered = ImGui::IsItemHovered();
		bool canvas_active = ImGui::IsItemActive();

		ImDrawList* dl = ImGui::GetWindowDrawList();

		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw grid
		float grid_size = 50.0f;
		for (float x = grid_size; x < canvas_size.x; x += grid_size) {
			dl->AddLine(ImVec2(canvas_pos.x + x, canvas_pos.y),
				ImVec2(canvas_pos.x + x, canvas_pos.y + canvas_size.y),
				IM_COL32(60, 60, 70, 255), 1.0f);
		}
		for (float y = grid_size; y < canvas_size.y; y += grid_size) {
			dl->AddLine(ImVec2(canvas_pos.x, canvas_pos.y + y),
				ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + y),
				IM_COL32(60, 60, 70, 255), 1.0f);
		}

		ImVec2 box_screen_pos(canvas_pos.x + box_pos.x, canvas_pos.y + box_pos.y);
		ImVec2 box_size(40, 40);
		ImRect box_rect(ImVec2(box_screen_pos.x - box_size.x * 0.5f, box_screen_pos.y - box_size.y * 0.5f),
			ImVec2(box_screen_pos.x + box_size.x * 0.5f, box_screen_pos.y + box_size.y * 0.5f));

		ImVec2 mouse = ImGui::GetMousePos();
		bool box_hovered = box_rect.Contains(mouse) && canvas_hovered;

		// Start dragging when clicking on box
		if (box_hovered && ImGui::IsMouseClicked(0)) {
			dragging = true;
			drag_offset = ImVec2(mouse.x - box_screen_pos.x, mouse.y - box_screen_pos.y);
		}

		if (dragging) {
			if (ImGui::IsMouseDown(0)) {
				// Update position while dragging
				box_pos.x = mouse.x - canvas_pos.x - drag_offset.x;
				box_pos.y = mouse.y - canvas_pos.y - drag_offset.y;
				// Clamp to canvas
				box_pos.x = ImClamp(box_pos.x, box_size.x * 0.5f, canvas_size.x - box_size.x * 0.5f);
				box_pos.y = ImClamp(box_pos.y, box_size.y * 0.5f, canvas_size.y - box_size.y * 0.5f);
				snap_target = box_pos;
			} else {
				// Release - snap to grid
				dragging = false;
				snap_target.x = ImFloor((box_pos.x + grid_size * 0.5f) / grid_size) * grid_size + grid_size * 0.5f;
				snap_target.y = ImFloor((box_pos.y + grid_size * 0.5f) / grid_size) * grid_size + grid_size * 0.5f;
				// Clamp snap target
				snap_target.x = ImClamp(snap_target.x, grid_size * 0.5f, canvas_size.x - grid_size * 0.5f);
				snap_target.y = ImClamp(snap_target.y, grid_size * 0.5f, canvas_size.y - grid_size * 0.5f);
			}
		}

		// Animate to snap target when not dragging
		if (!dragging) {
			ImGuiID id = ImGui::GetID("drag_snap_demo");
			box_pos = iam_tween_vec2(id, ImHashStr("pos"), snap_target, 0.25f,
				iam_ease_preset(iam_ease_out_back), iam_policy_crossfade, dt);
		}

		// Draw box
		ImU32 box_col = dragging ? IM_COL32(255, 200, 100, 255) : (box_hovered ? IM_COL32(120, 220, 255, 255) : IM_COL32(91, 194, 231, 255));
		dl->AddRectFilled(
			ImVec2(canvas_pos.x + box_pos.x - box_size.x * 0.5f, canvas_pos.y + box_pos.y - box_size.y * 0.5f),
			ImVec2(canvas_pos.x + box_pos.x + box_size.x * 0.5f, canvas_pos.y + box_pos.y + box_size.y * 0.5f),
			box_col, 4.0f);

		ImGui::TextDisabled("Drag box and release - snaps to grid with bounce");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 10: FRAME MANAGEMENT & OPTIMIZATION
// ============================================================

static void DocSection_FrameManagement()
{
	DocSectionHeader("FRAME MANAGEMENT & OPTIMIZATION",
		"Proper initialization, per-frame updates, memory management, and performance tips.");

	// --------------------------------------------------------
	// Frame Loop
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Frame Loop Setup"))
	{
		ImGui::TextWrapped(
			"Call these functions every frame in your main loop.");

		DocCodeSnippet(
			"void frame() {\n"
			"    float dt = ImGui::GetIO().DeltaTime;\n"
			"\n"
			"    // 1. Update animation systems (REQUIRED)\n"
			"    iam_update_begin_frame();\n"
			"\n"
			"    // 2. Update clip system (if using clips)\n"
			"    iam_clip_update(dt);\n"
			"\n"
			"    // 3. Your ImGui code with tweens/clips...\n"
			"\n"
			"    // 4. Periodic garbage collection (optional)\n"
			"    static float gc_timer = 0;\n"
			"    if ((gc_timer += dt) > 1.0f) {\n"
			"        iam_gc(600);      // Remove channels unused for 600 frames\n"
			"        iam_clip_gc(600); // Remove clip instances\n"
			"        gc_timer = 0;\n"
			"    }\n"
			"}");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Global Time Scale
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Global Time Scale"))
	{
		ImGui::TextWrapped(
			"Slow down or speed up all animations globally. Useful for debugging or effects.");

		DocCodeSnippet(
			"iam_set_global_time_scale(0.5f);  // Half speed\n"
			"iam_set_global_time_scale(2.0f);  // Double speed\n"
			"float scale = iam_get_global_time_scale();");

		ImGui::Separator();

		static float time_scale = 1.0f;
		if (ImGui::SliderFloat("Time Scale", &time_scale, 0.1f, 3.0f)) {
			iam_set_global_time_scale(time_scale);
		}
		if (ImGui::Button("Reset to 1.0")) {
			time_scale = 1.0f;
			iam_set_global_time_scale(1.0f);
		}

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Memory Management
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Memory Management"))
	{
		ImGui::TextWrapped(
			"Pre-allocate pools to avoid runtime allocations. Use GC to clean up stale channels.");

		DocCodeSnippet(
			"// Pre-allocate at startup\n"
			"iam_reserve(\n"
			"    1000,  // float channels\n"
			"    500,   // vec2 channels\n"
			"    200,   // vec4 channels\n"
			"    100,   // int channels\n"
			"    300    // color channels\n"
			");\n"
			"\n"
			"// Clip system\n"
			"iam_clip_init(256, 4096);  // clips, instances\n"
			"\n"
			"// Garbage collection\n"
			"iam_gc(600);       // Remove after 600 frames (~10s at 60fps)\n"
			"iam_clip_gc(600);\n"
			"\n"
			"// Clear all pools (scene transitions, level resets)\n"
			"iam_pool_clear();");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Lazy Initialization
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Lazy Initialization"))
	{
		ImGui::TextWrapped(
			"By default, channels are only created when the target differs from zero. "
			"Disable for immediate allocation.");

		DocCodeSnippet(
			"iam_set_lazy_init(true);   // Default: only allocate on non-zero\n"
			"iam_set_lazy_init(false);  // Always allocate immediately\n"
			"bool lazy = iam_is_lazy_init_enabled();");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Profiling
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Performance Profiling"))
	{
		ImGui::TextWrapped(
			"Built-in profiler for measuring animation system overhead.");

		DocCodeSnippet(
			"// Enable profiler\n"
			"iam_profiler_enable(true);\n"
			"\n"
			"// In your frame:\n"
			"iam_profiler_begin_frame();\n"
			"\n"
			"iam_profiler_begin(\"My Section\");\n"
			"// ... code to measure ...\n"
			"iam_profiler_end();\n"
			"\n"
			"iam_profiler_end_frame();");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 11: SCROLL ANIMATION
// ============================================================

static void DocSection_ScrollAnimation()
{
	DocSectionHeader("SCROLL ANIMATION",
		"Animate ImGui window scrolling with smooth easing. Perfect for 'scroll to top' buttons, "
		"navigating to specific sections, or animated content reveals.");

	// --------------------------------------------------------
	// Scroll To Position
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Scroll To Position"))
	{
		ImGui::TextWrapped(
			"Scroll the current window to a specific X or Y position with animation.");

		DocCodeSnippet(
			"// Scroll to Y position\n"
			"iam_scroll_to_y(target_y, duration, ease);\n"
			"\n"
			"// Scroll to X position\n"
			"iam_scroll_to_x(target_x, duration, ease);\n"
			"\n"
			"// Example: Scroll to 500px from top\n"
			"iam_scroll_to_y(500.0f, 0.5f, iam_ease_preset(iam_ease_out_cubic));");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Scroll To Top/Bottom
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Scroll To Top/Bottom"))
	{
		ImGui::TextWrapped(
			"Convenience functions to scroll to the beginning or end of content.");

		DocCodeSnippet(
			"// Scroll to top of window\n"
			"iam_scroll_to_top(0.3f);  // 0.3 second animation\n"
			"\n"
			"// Scroll to bottom of window\n"
			"iam_scroll_to_bottom(0.3f);\n"
			"\n"
			"// With custom easing\n"
			"iam_scroll_to_top(0.5f, iam_ease_preset(iam_ease_out_back));");

		ImGui::Separator();
		ImGui::Text("Interactive Example (scroll this documentation window):");

		if (ImGui::Button("Scroll to Top")) {
			iam_scroll_to_top(0.5f, iam_ease_preset(iam_ease_out_cubic));
		}
		ImGui::SameLine();
		if (ImGui::Button("Scroll to Bottom")) {
			iam_scroll_to_bottom(0.5f, iam_ease_preset(iam_ease_out_cubic));
		}

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 12: PER-AXIS EASING
// ============================================================

static void DocSection_PerAxisEasing()
{
	DocSectionHeader("PER-AXIS EASING",
		"Apply different easing curves to each component of vec2, vec4, or color values. "
		"Create complex motion like a ball with bouncy vertical movement but smooth horizontal.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Per-Axis Descriptor
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_ease_per_axis"))
	{
		ImGui::TextWrapped(
			"Structure holding separate easing descriptors for X, Y, Z, and W components.");

		DocCodeSnippet(
			"// Create per-axis easing\n"
			"iam_ease_per_axis ez;\n"
			"ez.x = iam_ease_preset(iam_ease_out_cubic);    // Smooth X\n"
			"ez.y = iam_ease_preset(iam_ease_out_bounce);   // Bouncy Y\n"
			"\n"
			"// Or use constructors\n"
			"iam_ease_per_axis ez_all(iam_ease_preset(iam_ease_linear));  // Same for all\n"
			"iam_ease_per_axis ez_xy(ease_x, ease_y);                     // For vec2\n"
			"iam_ease_per_axis ez_xyzw(ease_x, ease_y, ease_z, ease_w);   // For vec4");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Vec2 Per-Axis
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_tween_vec2_per_axis"))
	{
		ImGui::TextWrapped(
			"Animate a vec2 with different easing per axis. The X and Y components "
			"animate independently with their own curves.");

		DocCodeSnippet(
			"iam_ease_per_axis ez;\n"
			"ez.x = iam_ease_preset(iam_ease_out_cubic);   // Smooth horizontal\n"
			"ez.y = iam_ease_preset(iam_ease_out_bounce);  // Bouncy vertical\n"
			"\n"
			"ImVec2 pos = iam_tween_vec2_per_axis(\n"
			"    id, channel_id,\n"
			"    target,\n"
			"    duration, ez, policy, dt\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example (X: smooth, Y: bounce):");

		static ImVec2 target(180.0f, 80.0f);
		static int corner = 0;
		static float auto_timer = 0.0f;
		ImVec2 corners[] = {
			ImVec2(30.0f, 30.0f),
			ImVec2(180.0f, 30.0f),
			ImVec2(180.0f, 80.0f),
			ImVec2(30.0f, 80.0f)
		};

		// Auto-advance every 1.5 seconds
		auto_timer += dt;
		if (auto_timer > 1.5f) {
			auto_timer = 0.0f;
			corner = (corner + 1) % 4;
			target = corners[corner];
		}

		if (ImGui::Button("Next Corner##peraxis")) {
			corner = (corner + 1) % 4;
			target = corners[corner];
			auto_timer = 0.0f;
		}

		iam_ease_per_axis ez;
		ez.x = iam_ease_preset(iam_ease_out_cubic);
		ez.y = iam_ease_preset(iam_ease_out_bounce);

		ImGuiID id = ImGui::GetID("peraxis_demo");
		ImVec2 pos = iam_tween_vec2_per_axis(id, ImHashStr("pos"), target, 1.0f, ez, iam_policy_crossfade, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(220.0f, 120.0f);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);
		dl->AddCircleFilled(ImVec2(canvas_pos.x + pos.x, canvas_pos.y + pos.y), 12.0f,
			IM_COL32(91, 194, 231, 255));

		ImGui::Dummy(canvas_size);
		ImGui::Text("Notice: Y bounces, X is smooth (auto-animating)");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Vec4/Color Per-Axis
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_tween_vec4/color_per_axis"))
	{
		ImGui::TextWrapped(
			"Same concept for vec4 and colors. Each of the 4 components gets its own easing.");

		DocCodeSnippet(
			"// Vec4 with per-axis easing\n"
			"ImVec4 rect = iam_tween_vec4_per_axis(\n"
			"    id, channel_id, target, duration, ez, policy, dt\n"
			");\n"
			"\n"
			"// Color with per-axis easing (R, G, B, A)\n"
			"iam_ease_per_axis ez;\n"
			"ez.x = iam_ease_preset(iam_ease_out_cubic);  // Red\n"
			"ez.y = iam_ease_preset(iam_ease_out_quad);   // Green\n"
			"ez.z = iam_ease_preset(iam_ease_linear);     // Blue\n"
			"ez.w = iam_ease_preset(iam_ease_out_expo);   // Alpha\n"
			"\n"
			"ImVec4 color = iam_tween_color_per_axis(\n"
			"    id, channel_id, target_srgb, duration, ez, policy, color_space, dt\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example (R: bounce, G: elastic, B: linear, A: expo):");

		static ImVec4 color_target(1.0f, 0.0f, 1.0f, 1.0f);
		static bool color_toggle = false;
		static float color_anim_timer = 0.0f;

		// Auto-toggle every 2 seconds
		color_anim_timer += dt;
		if (color_anim_timer > 2.0f) {
			color_anim_timer = 0.0f;
			color_toggle = !color_toggle;
			color_target = color_toggle ? ImVec4(0.0f, 1.0f, 0.0f, 0.3f) : ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
		}

		if (ImGui::Button("Toggle Color##peraxis_color")) {
			color_toggle = !color_toggle;
			color_target = color_toggle ? ImVec4(0.0f, 1.0f, 0.0f, 0.3f) : ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
			color_anim_timer = 0.0f;
		}

		iam_ease_per_axis ez_color;
		ez_color.x = iam_ease_preset(iam_ease_out_bounce);   // Red: bouncy
		ez_color.y = iam_ease_preset(iam_ease_out_elastic);  // Green: elastic
		ez_color.z = iam_ease_preset(iam_ease_linear);       // Blue: linear
		ez_color.w = iam_ease_preset(iam_ease_out_expo);     // Alpha: exponential

		ImGuiID color_id = ImGui::GetID("peraxis_color_demo");
		ImVec4 color = iam_tween_color_per_axis(color_id, ImHashStr("col"), color_target, 1.5f,
			ez_color, iam_policy_crossfade, iam_col_srgb, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(500.0f, 200.0f);  // 300% larger
		ImDrawList* dl = ImGui::GetWindowDrawList();

		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 8.0f);

		// Draw large color swatch (scaled up)
		float swatch_margin = 20.0f;
		float swatch_size = 160.0f;
		ImU32 swatch_col = ImGui::ColorConvertFloat4ToU32(color);
		dl->AddRectFilled(
			ImVec2(canvas_pos.x + swatch_margin, canvas_pos.y + swatch_margin),
			ImVec2(canvas_pos.x + swatch_margin + swatch_size, canvas_pos.y + swatch_margin + swatch_size),
			swatch_col, 8.0f);
		dl->AddRect(
			ImVec2(canvas_pos.x + swatch_margin, canvas_pos.y + swatch_margin),
			ImVec2(canvas_pos.x + swatch_margin + swatch_size, canvas_pos.y + swatch_margin + swatch_size),
			IM_COL32(255, 255, 255, 100), 8.0f, 0, 2.0f);

		// Draw individual channel bars (scaled up)
		float bar_x = canvas_pos.x + swatch_margin + swatch_size + 30.0f;
		float bar_w = 150.0f;
		float bar_h = 28.0f;
		float bar_spacing = 42.0f;
		float bar_y_start = canvas_pos.y + 25.0f;

		// Red bar
		dl->AddRectFilled(ImVec2(bar_x, bar_y_start), ImVec2(bar_x + bar_w, bar_y_start + bar_h),
			IM_COL32(60, 60, 70, 255), 4.0f);
		dl->AddRectFilled(ImVec2(bar_x, bar_y_start), ImVec2(bar_x + bar_w * color.x, bar_y_start + bar_h),
			IM_COL32(255, 80, 80, 255), 4.0f);
		dl->AddText(ImVec2(bar_x + bar_w + 10, bar_y_start + 6), IM_COL32(200, 200, 200, 255), "R (bounce)");

		// Green bar
		dl->AddRectFilled(ImVec2(bar_x, bar_y_start + bar_spacing), ImVec2(bar_x + bar_w, bar_y_start + bar_spacing + bar_h),
			IM_COL32(60, 60, 70, 255), 4.0f);
		dl->AddRectFilled(ImVec2(bar_x, bar_y_start + bar_spacing), ImVec2(bar_x + bar_w * color.y, bar_y_start + bar_spacing + bar_h),
			IM_COL32(80, 255, 80, 255), 4.0f);
		dl->AddText(ImVec2(bar_x + bar_w + 10, bar_y_start + bar_spacing + 6), IM_COL32(200, 200, 200, 255), "G (elastic)");

		// Blue bar
		dl->AddRectFilled(ImVec2(bar_x, bar_y_start + bar_spacing * 2), ImVec2(bar_x + bar_w, bar_y_start + bar_spacing * 2 + bar_h),
			IM_COL32(60, 60, 70, 255), 4.0f);
		dl->AddRectFilled(ImVec2(bar_x, bar_y_start + bar_spacing * 2), ImVec2(bar_x + bar_w * color.z, bar_y_start + bar_spacing * 2 + bar_h),
			IM_COL32(80, 80, 255, 255), 4.0f);
		dl->AddText(ImVec2(bar_x + bar_w + 10, bar_y_start + bar_spacing * 2 + 6), IM_COL32(200, 200, 200, 255), "B (linear)");

		// Alpha bar
		dl->AddRectFilled(ImVec2(bar_x, bar_y_start + bar_spacing * 3), ImVec2(bar_x + bar_w, bar_y_start + bar_spacing * 3 + bar_h),
			IM_COL32(60, 60, 70, 255), 4.0f);
		dl->AddRectFilled(ImVec2(bar_x, bar_y_start + bar_spacing * 3), ImVec2(bar_x + bar_w * color.w, bar_y_start + bar_spacing * 3 + bar_h),
			IM_COL32(200, 200, 200, 255), 4.0f);
		dl->AddText(ImVec2(bar_x + bar_w + 10, bar_y_start + bar_spacing * 3 + 6), IM_COL32(200, 200, 200, 255), "A (expo)");

		ImGui::Dummy(canvas_size);
		ImGui::TextDisabled("Each channel animates with its own easing curve");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 13: ARC-LENGTH PARAMETERIZATION
// ============================================================

static void DocSection_ArcLength()
{
	DocSectionHeader("ARC-LENGTH PARAMETERIZATION",
		"By default, path parameter t doesn't map linearly to distance. Arc-length parameterization "
		"enables constant-speed animation regardless of curve complexity.");

	// --------------------------------------------------------
	// Building Arc-Length LUT
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Building Arc-Length LUT"))
	{
		ImGui::TextWrapped(
			"Build a lookup table (LUT) to convert between arc-length distance and parameter t. "
			"This is done once per path and enables constant-speed animation.");

		DocCodeSnippet(
			"// Build LUT with 64 subdivisions (default)\n"
			"iam_path_build_arc_lut(PATH_ID, 64);\n"
			"\n"
			"// Higher subdivisions = more accuracy, more memory\n"
			"iam_path_build_arc_lut(PATH_ID, 128);\n"
			"\n"
			"// Check if LUT exists\n"
			"if (iam_path_has_arc_lut(PATH_ID)) {\n"
			"    // Can use distance-based functions\n"
			"}");

		ImGui::Separator();
		ImGui::Text("Interactive Example: Path Length vs LUT Resolution");

		// Create a complex bezier curve for this demo
		static ImGuiID const DOC_PATH_LUT_DEMO = ImHashStr("doc_path_lut_demo");
		static bool lut_demo_init = false;
		static int lut_resolution = 64;
		static float path_lengths[5] = { 0 };  // Store lengths for different resolutions
		int resolutions[] = { 8, 16, 32, 64, 128 };

		if (!lut_demo_init) {
			// Create a complex curve with tight turns
			iam_path::begin(DOC_PATH_LUT_DEMO, ImVec2(20, 60))
				.cubic_to(ImVec2(60, 10), ImVec2(100, 110), ImVec2(140, 60))
				.cubic_to(ImVec2(180, 10), ImVec2(220, 110), ImVec2(260, 60))
				.end();

			// Calculate path lengths at different resolutions
			for (int i = 0; i < 5; i++) {
				iam_path_build_arc_lut(DOC_PATH_LUT_DEMO, resolutions[i]);
				path_lengths[i] = iam_path_length(DOC_PATH_LUT_DEMO);
			}
			lut_demo_init = true;
		}

		// Resolution selector
		ImGui::Text("LUT Resolution:");
		int res_idx = 3;  // Default to 64
		for (int i = 0; i < 5; i++) {
			if (resolutions[i] == lut_resolution) res_idx = i;
		}
		if (ImGui::RadioButton("8##lut", &res_idx, 0)) lut_resolution = 8;
		ImGui::SameLine();
		if (ImGui::RadioButton("16##lut", &res_idx, 1)) lut_resolution = 16;
		ImGui::SameLine();
		if (ImGui::RadioButton("32##lut", &res_idx, 2)) lut_resolution = 32;
		ImGui::SameLine();
		if (ImGui::RadioButton("64##lut", &res_idx, 3)) lut_resolution = 64;
		ImGui::SameLine();
		if (ImGui::RadioButton("128##lut", &res_idx, 4)) lut_resolution = 128;

		// Rebuild LUT with selected resolution
		iam_path_build_arc_lut(DOC_PATH_LUT_DEMO, lut_resolution);
		float current_length = iam_path_length(DOC_PATH_LUT_DEMO);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(280, 100);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw the curve
		ImVec2 prev = iam_path_evaluate(DOC_PATH_LUT_DEMO, 0.0f);
		for (int i = 1; i <= 50; i++) {
			float t = (float)i / 50.0f;
			ImVec2 curr = iam_path_evaluate(DOC_PATH_LUT_DEMO, t);
			dl->AddLine(
				ImVec2(canvas_pos.x + prev.x, canvas_pos.y + prev.y),
				ImVec2(canvas_pos.x + curr.x, canvas_pos.y + curr.y),
				IM_COL32(91, 194, 231, 255), 2.5f);
			prev = curr;
		}

		// Draw sample points based on resolution (show how the LUT samples the curve)
		for (int i = 0; i <= lut_resolution; i++) {
			float t = (float)i / (float)lut_resolution;
			ImVec2 pt = iam_path_evaluate(DOC_PATH_LUT_DEMO, t);
			ImU32 col = (i == 0 || i == lut_resolution) ? IM_COL32(255, 200, 100, 255) : IM_COL32(255, 255, 255, 150);
			float radius = (i == 0 || i == lut_resolution) ? 4.0f : 2.0f;
			dl->AddCircleFilled(ImVec2(canvas_pos.x + pt.x, canvas_pos.y + pt.y), radius, col);
		}

		ImGui::Dummy(canvas_size);

		// Show path length comparison
		ImGui::Text("Path Length: %.2f px", current_length);
		ImGui::Text("Length at different resolutions:");
		for (int i = 0; i < 5; i++) {
			float diff = path_lengths[i] - path_lengths[4];  // Diff from highest res
			ImGui::TextDisabled("  %3d subdivs: %.2f px (%.2f from true)", resolutions[i], path_lengths[i], diff);
		}
		ImGui::TextDisabled("Higher resolution = more accurate length calculation");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Distance-Based Evaluation
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Distance-Based Evaluation"))
	{
		ImGui::TextWrapped(
			"Once a LUT is built, evaluate paths using distance instead of parameter t. "
			"This gives constant speed regardless of curve curvature.");

		DocCodeSnippet(
			"// Get total path length\n"
			"float total_length = iam_path_length(PATH_ID);\n"
			"\n"
			"// Convert distance to t parameter\n"
			"float t = iam_path_distance_to_t(PATH_ID, distance);\n"
			"\n"
			"// Or directly evaluate at distance\n"
			"ImVec2 pos = iam_path_evaluate_at_distance(PATH_ID, distance);\n"
			"float angle = iam_path_angle_at_distance(PATH_ID, distance);\n"
			"ImVec2 tangent = iam_path_tangent_at_distance(PATH_ID, distance);\n"
			"\n"
			"// Example: Constant speed animation\n"
			"float speed = 100.0f;  // pixels per second\n"
			"static float traveled = 0.0f;\n"
			"traveled += speed * dt;\n"
			"if (traveled > total_length) traveled = 0.0f;\n"
			"ImVec2 pos = iam_path_evaluate_at_distance(PATH_ID, traveled);");

		ImGui::Separator();
		ImGui::Text("Interactive Comparison: t-based vs Constant Speed (Arc-Length)");

		// Create path with VERY complex curvature to exaggerate the difference
		static ImGuiID const DOC_PATH_ARC_DEMO = ImHashStr("doc_path_arc_demo");
		static bool arc_path_init = false;
		if (!arc_path_init) {
			// Path with multiple tight loops followed by long straight
			// The loops have HIGH curvature (lots of t-change for little distance)
			// The straight has LOW curvature (little t-change for lots of distance)
			iam_path::begin(DOC_PATH_ARC_DEMO, ImVec2(15, 50))
				.cubic_to(ImVec2(15, 10), ImVec2(35, 10), ImVec2(35, 50))   // Loop 1 up
				.cubic_to(ImVec2(35, 90), ImVec2(55, 90), ImVec2(55, 50))   // Loop 1 down
				.cubic_to(ImVec2(55, 10), ImVec2(75, 10), ImVec2(75, 50))   // Loop 2 up
				.cubic_to(ImVec2(75, 90), ImVec2(95, 90), ImVec2(95, 50))   // Loop 2 down
				.line_to(ImVec2(320, 50))  // Long straight (most of the distance!)
				.end();
			iam_path_build_arc_lut(DOC_PATH_ARC_DEMO, 512);
			arc_path_init = true;
		}

		float dt = GetDocDeltaTime();
		float path_len = iam_path_length(DOC_PATH_ARC_DEMO);

		// t-based: complete in fixed 5 seconds (t goes 0->1)
		static float anim_t = 0.0f;
		static float traveled_dist = 0.0f;
		static bool arc_paused = false;

		if (ImGui::Button("Reset##arc_demo")) {
			anim_t = 0.0f;
			traveled_dist = 0.0f;
		}
		ImGui::SameLine();
		if (ImGui::Button(arc_paused ? "Resume##arc_demo" : "Pause##arc_demo")) {
			arc_paused = !arc_paused;
		}

		if (!arc_paused) {
			anim_t += dt * 0.2f;  // 5 second cycle
			if (anim_t > 1.0f) anim_t = 0.0f;

			// Constant speed: travel at 80 pixels/second
			float speed = 80.0f;  // pixels per second
			traveled_dist += speed * dt;
			if (traveled_dist > path_len) traveled_dist = 0.0f;
		}

		// Calculate positions
		ImVec2 pos_t = iam_path_evaluate(DOC_PATH_ARC_DEMO, anim_t);
		ImVec2 pos_arc = iam_path_evaluate_at_distance(DOC_PATH_ARC_DEMO, traveled_dist);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(340, 110);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw start/end markers
		ImVec2 path_start = iam_path_evaluate(DOC_PATH_ARC_DEMO, 0.0f);
		ImVec2 path_end = iam_path_evaluate(DOC_PATH_ARC_DEMO, 1.0f);
		dl->AddCircle(ImVec2(canvas_pos.x + path_start.x, canvas_pos.y + path_start.y), 10.0f,
			IM_COL32(255, 255, 255, 150), 0, 2.0f);
		dl->AddCircle(ImVec2(canvas_pos.x + path_end.x, canvas_pos.y + path_end.y), 10.0f,
			IM_COL32(255, 255, 0, 200), 0, 2.0f);
		dl->AddText(ImVec2(canvas_pos.x + path_end.x - 20, canvas_pos.y + 92),
			IM_COL32(255, 255, 0, 255), "FINISH");

		// Draw path with more detail
		ImVec2 prev = iam_path_evaluate(DOC_PATH_ARC_DEMO, 0.0f);
		for (int i = 1; i <= 100; i++) {
			float t = (float)i / 100.0f;
			ImVec2 curr = iam_path_evaluate(DOC_PATH_ARC_DEMO, t);
			dl->AddLine(
				ImVec2(canvas_pos.x + prev.x, canvas_pos.y + prev.y),
				ImVec2(canvas_pos.x + curr.x, canvas_pos.y + curr.y),
				IM_COL32(80, 80, 100, 255), 2.0f);
			prev = curr;
		}

		// Draw t-based position (red) - above path
		dl->AddCircleFilled(ImVec2(canvas_pos.x + pos_t.x, canvas_pos.y + pos_t.y - 12), 10.0f,
			IM_COL32(255, 80, 80, 255));

		// Draw constant-speed position (green) - below path
		dl->AddCircleFilled(ImVec2(canvas_pos.x + pos_arc.x, canvas_pos.y + pos_arc.y + 12), 10.0f,
			IM_COL32(80, 255, 80, 255));

		ImGui::Dummy(canvas_size);

		// Show progress for both
		float t_pct = anim_t * 100.0f;
		float arc_pct = (traveled_dist / path_len) * 100.0f;
		ImGui::Text("Red (t-based): %.0f%% | Green (constant speed): %.0f%%", t_pct, arc_pct);
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Red: Slow in loops, FAST on straight (arrives first!)");
		ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Green: Constant 80px/sec (steady pace)");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 14: ANIMATION LAYERING
// ============================================================

static void DocSection_Layering()
{
	DocSectionHeader("ANIMATION LAYERING",
		"Blend multiple animation instances together with weighted contributions. "
		"Perfect for animation state machines, crossfades between clips, or additive effects.");

	// --------------------------------------------------------
	// Layer API
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Layer Blending"))
	{
		ImGui::TextWrapped(
			"Blend multiple animation instances into a single output. Each instance "
			"contributes based on its weight. Weights are normalized automatically.");

		DocCodeSnippet(
			"// Start blending into a target instance\n"
			"iam_layer_begin(target_instance_id);\n"
			"\n"
			"// Add source instances with weights\n"
			"iam_layer_add(idle_inst, 0.3f);    // 30% idle\n"
			"iam_layer_add(walk_inst, 0.7f);    // 70% walk\n"
			"\n"
			"// Finalize blending\n"
			"iam_layer_end(target_instance_id);\n"
			"\n"
			"// Get blended values\n"
			"float value;\n"
			"if (iam_get_blended_float(target_instance_id, CHANNEL, &value)) {\n"
			"    // Use blended value\n"
			"}\n"
			"\n"
			"// Also available:\n"
			"iam_get_blended_vec2(target_id, channel, &out);\n"
			"iam_get_blended_vec4(target_id, channel, &out);\n"
			"iam_get_blended_int(target_id, channel, &out);");

		ImGui::Separator();
		ImGui::Text("Interactive Example (blend two oscillations):");

		float dt = GetDocDeltaTime();

		// Simple demo: blend between two oscillating values
		static float layer_time = 0.0f;
		layer_time += dt;

		// Value A: slow sine wave (left-right)
		float val_a = sinf(layer_time * 1.5f) * 60.0f;
		// Value B: fast sine wave (up-down motion pattern)
		float val_b = sinf(layer_time * 4.0f) * 40.0f;

		static float layer_blend_w = 0.5f;
		static bool layer_auto_blend = true;
		ImGui::Checkbox("Auto Blend##layerblend", &layer_auto_blend);
		if (layer_auto_blend) {
			// Oscillate blend weight
			layer_blend_w = (sinf(layer_time * 0.8f) + 1.0f) * 0.5f;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120);
		ImGui::SliderFloat("Weight##layerblend", &layer_blend_w, 0.0f, 1.0f);

		// Blended value
		float blended = val_a * (1.0f - layer_blend_w) + val_b * layer_blend_w;

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(220, 80);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		float center_x = canvas_pos.x + canvas_size.x * 0.5f;
		float center_y = canvas_pos.y + canvas_size.y * 0.5f;

		// Draw A (red, semi-transparent)
		dl->AddCircleFilled(ImVec2(center_x + val_a, center_y - 15), 8.0f,
			IM_COL32(255, 100, 100, (int)(100 * (1.0f - layer_blend_w) + 50)));
		// Draw B (blue, semi-transparent)
		dl->AddCircleFilled(ImVec2(center_x + val_b, center_y + 15), 8.0f,
			IM_COL32(100, 100, 255, (int)(100 * layer_blend_w + 50)));
		// Draw blended (cyan, solid)
		dl->AddCircleFilled(ImVec2(center_x + blended, center_y), 10.0f,
			IM_COL32(91, 194, 231, 255));

		ImGui::Dummy(canvas_size);
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "A:%.0f%%", (1.0f - layer_blend_w) * 100);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "B:%.0f%%", layer_blend_w * 100);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.36f, 0.76f, 0.9f, 1.0f), "Blended");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Instance Weight
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Instance Weight"))
	{
		ImGui::TextWrapped(
			"Set the weight of an individual instance for blending purposes.");

		DocCodeSnippet(
			"iam_instance inst = iam_play(CLIP_ID, INST_ID);\n"
			"\n"
			"// Set weight for blending\n"
			"inst.set_weight(0.5f);  // 50% contribution\n"
			"\n"
			"// Animate weight for crossfade\n"
			"float weight = iam_tween_float(id, CH_WEIGHT, target_weight, 0.3f, ease, policy, dt);\n"
			"inst.set_weight(weight);");

		ImGui::Separator();
		ImGui::Text("Interactive Example: Blend two positions with weights");

		static ImGuiID const DOC_CLIP_LAYER_A = ImHashStr("doc_clip_layer_a");
		static ImGuiID const DOC_CLIP_LAYER_B = ImHashStr("doc_clip_layer_b");
		static ImGuiID const DOC_CH_LAYER_X = ImHashStr("doc_ch_layer_x");
		static ImGuiID const DOC_CH_LAYER_Y = ImHashStr("doc_ch_layer_y");
		static bool layer_clips_init = false;
		if (!layer_clips_init) {
			// Clip A: Circular motion
			iam_clip::begin(DOC_CLIP_LAYER_A)
				.key_float(DOC_CH_LAYER_X, 0.0f, 100.0f + 60.0f, iam_ease_linear)
				.key_float(DOC_CH_LAYER_X, 0.5f, 100.0f - 60.0f)
				.key_float(DOC_CH_LAYER_X, 1.0f, 100.0f + 60.0f)
				.key_float(DOC_CH_LAYER_Y, 0.0f, 50.0f, iam_ease_linear)
				.key_float(DOC_CH_LAYER_Y, 0.25f, 50.0f - 30.0f)
				.key_float(DOC_CH_LAYER_Y, 0.75f, 50.0f + 30.0f)
				.key_float(DOC_CH_LAYER_Y, 1.0f, 50.0f)
				.set_loop(true, iam_dir_normal, -1)
				.end();

			// Clip B: Diagonal motion
			iam_clip::begin(DOC_CLIP_LAYER_B)
				.key_float(DOC_CH_LAYER_X, 0.0f, 30.0f, iam_ease_in_out_quad)
				.key_float(DOC_CH_LAYER_X, 1.5f, 170.0f)
				.key_float(DOC_CH_LAYER_Y, 0.0f, 80.0f, iam_ease_in_out_quad)
				.key_float(DOC_CH_LAYER_Y, 1.5f, 20.0f)
				.set_loop(true, iam_dir_alternate, -1)
				.end();

			layer_clips_init = true;
		}

		static ImGuiID layer_inst_a = ImHashStr("doc_layer_inst_a");
		static ImGuiID layer_inst_b = ImHashStr("doc_layer_inst_b");
		static bool layer_playing = false;
		static bool layer_auto_started = false;
		static float blend_weight = 0.5f;

		// Auto-start on first view
		if (!layer_auto_started) {
			iam_play(DOC_CLIP_LAYER_A, layer_inst_a);
			iam_play(DOC_CLIP_LAYER_B, layer_inst_b);
			layer_playing = true;
			layer_auto_started = true;
		}

		if (ImGui::Button(layer_playing ? "Stop##layer" : "Play Both##layer")) {
			if (!layer_playing) {
				iam_play(DOC_CLIP_LAYER_A, layer_inst_a);
				iam_play(DOC_CLIP_LAYER_B, layer_inst_b);
				layer_playing = true;
			} else {
				iam_get_instance(layer_inst_a).stop();
				iam_get_instance(layer_inst_b).stop();
				layer_playing = false;
			}
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150);
		ImGui::SliderFloat("A <-> B##layer", &blend_weight, 0.0f, 1.0f);

		// Get individual positions
		float x_a = 100.0f, y_a = 50.0f;
		float x_b = 100.0f, y_b = 50.0f;
		iam_instance inst_a = iam_get_instance(layer_inst_a);
		iam_instance inst_b = iam_get_instance(layer_inst_b);
		if (inst_a.valid()) {
			inst_a.get_float(DOC_CH_LAYER_X, &x_a);
			inst_a.get_float(DOC_CH_LAYER_Y, &y_a);
		}
		if (inst_b.valid()) {
			inst_b.get_float(DOC_CH_LAYER_X, &x_b);
			inst_b.get_float(DOC_CH_LAYER_Y, &y_b);
		}

		// Blend positions manually
		float x_blend = x_a * (1.0f - blend_weight) + x_b * blend_weight;
		float y_blend = y_a * (1.0f - blend_weight) + y_b * blend_weight;

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(200, 100);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw A position (red, transparent based on weight)
		int alpha_a = (int)(255 * (1.0f - blend_weight) * 0.5f);
		dl->AddCircleFilled(ImVec2(canvas_pos.x + x_a, canvas_pos.y + y_a), 8.0f,
			IM_COL32(255, 100, 100, alpha_a));

		// Draw B position (blue, transparent based on weight)
		int alpha_b = (int)(255 * blend_weight * 0.5f);
		dl->AddCircleFilled(ImVec2(canvas_pos.x + x_b, canvas_pos.y + y_b), 8.0f,
			IM_COL32(100, 100, 255, alpha_b));

		// Draw blended position (solid cyan)
		dl->AddCircleFilled(ImVec2(canvas_pos.x + x_blend, canvas_pos.y + y_blend), 10.0f,
			IM_COL32(91, 194, 231, 255));

		ImGui::Dummy(canvas_size);

		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Red: Clip A (circular)");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "Blue: Clip B (diagonal)");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.36f, 0.76f, 0.9f, 1.0f), "Cyan: Blended");

		ImGui::Text("Clip A timeline:");
		iam_show_debug_timeline(layer_inst_a);
		ImGui::Text("Clip B timeline:");
		iam_show_debug_timeline(layer_inst_b);

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 15: CLIP CALLBACKS
// ============================================================

static void DocSection_ClipCallbacks()
{
	DocSectionHeader("CLIP CALLBACKS",
		"Execute code at specific points in clip playback: when it begins, every update, "
		"and when it completes. Essential for game logic synchronization.");

	// --------------------------------------------------------
	// Callback Types
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Callback Types"))
	{
		ImGui::TextWrapped(
			"Three callback points are available for each clip.");

		DocCodeSnippet(
			"// Callback signature\n"
			"void my_callback(ImGuiID inst_id, void* user_data) {\n"
			"    // Your code here\n"
			"}\n"
			"\n"
			"iam_clip::begin(CLIP_ID)\n"
			"    .key_float(...)\n"
			"    // Called once when clip starts playing\n"
			"    .on_begin(my_callback, user_data)\n"
			"    // Called every frame while clip is playing\n"
			"    .on_update(my_callback, user_data)\n"
			"    // Called once when clip finishes (or stops)\n"
			"    .on_complete(my_callback, user_data)\n"
			"    .end();");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Use Cases
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Common Use Cases"))
	{
		ImGui::TextWrapped(
			"Examples of how to use clip callbacks effectively.");

		DocCodeSnippet(
			"// Play sound when animation starts\n"
			".on_begin([](ImGuiID, void*) {\n"
			"    PlaySound(\"whoosh.wav\");\n"
			"}, nullptr)\n"
			"\n"
			"// Update game state every frame\n"
			".on_update([](ImGuiID inst_id, void* user) {\n"
			"    auto* obj = (GameObject*)user;\n"
			"    iam_instance inst = iam_get_instance(inst_id);\n"
			"    ImVec2 pos;\n"
			"    if (inst.get_vec2(CH_POS, &pos))\n"
			"        obj->position = pos;\n"
			"}, game_object)\n"
			"\n"
			"// Trigger next action when complete\n"
			".on_complete([](ImGuiID, void* user) {\n"
			"    auto* state = (AnimState*)user;\n"
			"    state->OnAnimationComplete();\n"
			"}, anim_state)");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Interactive Example
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Interactive Example##clip_callbacks"))
	{
		ImGui::TextWrapped(
			"Watch the callback indicators light up as the animation plays.");

		// Callback state
		struct CallbackState {
			int begin_count = 0;
			int update_count = 0;
			int complete_count = 0;
			float begin_flash = 0.0f;
			float update_flash = 0.0f;
			float complete_flash = 0.0f;
		};
		static CallbackState cb_state;

		static ImGuiID const DOC_CLIP_CALLBACK = ImHashStr("doc_clip_callback");
		static ImGuiID const DOC_CH_CB_X = ImHashStr("doc_ch_cb_x");
		static bool callback_clip_init = false;
		if (!callback_clip_init) {
			iam_clip::begin(DOC_CLIP_CALLBACK)
				.key_float(DOC_CH_CB_X, 0.0f, 20.0f, iam_ease_out_cubic)
				.key_float(DOC_CH_CB_X, 2.0f, 180.0f)
				.on_begin([](ImGuiID, void* user) {
					auto* s = (CallbackState*)user;
					s->begin_count++;
					s->begin_flash = 1.0f;
				}, &cb_state)
				.on_update([](ImGuiID, void* user) {
					auto* s = (CallbackState*)user;
					s->update_count++;
					s->update_flash = 1.0f;
				}, &cb_state)
				.on_complete([](ImGuiID, void* user) {
					auto* s = (CallbackState*)user;
					s->complete_count++;
					s->complete_flash = 1.0f;
				}, &cb_state)
				.end();
			callback_clip_init = true;
		}

		float dt = GetDocDeltaTime();

		// Decay flash values
		cb_state.begin_flash = ImMax(0.0f, cb_state.begin_flash - dt * 3.0f);
		cb_state.update_flash = ImMax(0.0f, cb_state.update_flash - dt * 10.0f);
		cb_state.complete_flash = ImMax(0.0f, cb_state.complete_flash - dt * 3.0f);

		static ImGuiID callback_inst = ImHashStr("doc_callback_inst");

		if (ImGui::Button("Play##callback")) {
			iam_play(DOC_CLIP_CALLBACK, callback_inst);
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Counters##callback")) {
			cb_state.begin_count = 0;
			cb_state.update_count = 0;
			cb_state.complete_count = 0;
		}

		// Draw callback indicators
		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		// Indicator boxes - wider to fit text with DPI scaling
		float box_w = 150.0f, box_h = 70.0f, gap = 15.0f;
		ImVec2 box_begin(canvas_pos.x, canvas_pos.y);
		ImVec2 box_update(canvas_pos.x + box_w + gap, canvas_pos.y);
		ImVec2 box_complete(canvas_pos.x + 2 * (box_w + gap), canvas_pos.y);

		ImFont* font = ImGui::GetFont();
		float font_size = ImGui::GetFontSize();

		// Begin box
		ImU32 col_begin = IM_COL32(100 + (int)(155 * cb_state.begin_flash), 60, 60, 255);
		dl->AddRectFilled(box_begin, ImVec2(box_begin.x + box_w, box_begin.y + box_h), col_begin, 6.0f);
		dl->AddRect(box_begin, ImVec2(box_begin.x + box_w, box_begin.y + box_h), IM_COL32(200, 100, 100, 255), 6.0f, 0, 2.0f);
		dl->AddText(font, font_size * 1.1f, ImVec2(box_begin.x + 20, box_begin.y + 14), IM_COL32_WHITE, "on_begin");
		char buf[32];
		snprintf(buf, sizeof(buf), "Count: %d", cb_state.begin_count);
		dl->AddText(font, font_size, ImVec2(box_begin.x + 20, box_begin.y + 42), IM_COL32(220, 220, 220, 255), buf);

		// Update box
		ImU32 col_update = IM_COL32(60, 100 + (int)(155 * cb_state.update_flash), 60, 255);
		dl->AddRectFilled(box_update, ImVec2(box_update.x + box_w, box_update.y + box_h), col_update, 6.0f);
		dl->AddRect(box_update, ImVec2(box_update.x + box_w, box_update.y + box_h), IM_COL32(100, 200, 100, 255), 6.0f, 0, 2.0f);
		dl->AddText(font, font_size * 1.1f, ImVec2(box_update.x + 20, box_update.y + 14), IM_COL32_WHITE, "on_update");
		snprintf(buf, sizeof(buf), "Count: %d", cb_state.update_count);
		dl->AddText(font, font_size, ImVec2(box_update.x + 20, box_update.y + 42), IM_COL32(220, 220, 220, 255), buf);

		// Complete box
		ImU32 col_complete = IM_COL32(60, 60, 100 + (int)(155 * cb_state.complete_flash), 255);
		dl->AddRectFilled(box_complete, ImVec2(box_complete.x + box_w, box_complete.y + box_h), col_complete, 6.0f);
		dl->AddRect(box_complete, ImVec2(box_complete.x + box_w, box_complete.y + box_h), IM_COL32(100, 100, 200, 255), 6.0f, 0, 2.0f);
		dl->AddText(font, font_size * 1.1f, ImVec2(box_complete.x + 20, box_complete.y + 14), IM_COL32_WHITE, "on_complete");
		snprintf(buf, sizeof(buf), "Count: %d", cb_state.complete_count);
		dl->AddText(font, font_size, ImVec2(box_complete.x + 20, box_complete.y + 42), IM_COL32(220, 220, 220, 255), buf);

		ImGui::Dummy(ImVec2(3 * box_w + 2 * gap, box_h + 15));

		// Draw animation
		float x = 20.0f;
		iam_instance inst = iam_get_instance(callback_inst);
		if (inst.valid()) {
			inst.get_float(DOC_CH_CB_X, &x);
		}

		ImVec2 anim_pos = ImGui::GetCursorScreenPos();
		dl->AddRectFilled(anim_pos, ImVec2(anim_pos.x + 200, anim_pos.y + 40),
			IM_COL32(30, 30, 40, 255), 4.0f);
		dl->AddCircleFilled(ImVec2(anim_pos.x + x, anim_pos.y + 20), 12.0f,
			IM_COL32(91, 194, 231, 255));
		ImGui::Dummy(ImVec2(200, 40));

		iam_show_debug_timeline(callback_inst);

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 16: ANCHOR-RELATIVE KEYFRAMES
// ============================================================

static void DocSection_AnchorRelativeKeyframes()
{
	DocSectionHeader("ANCHOR-RELATIVE KEYFRAMES",
		"Define keyframe values as percentages of window/viewport size plus pixel offsets. "
		"Animations automatically adapt when containers resize.");

	// --------------------------------------------------------
	// Relative Keyframes
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("key_*_rel Functions"))
	{
		ImGui::TextWrapped(
			"Create keyframes with values relative to an anchor space (window, viewport, etc.).");

		DocCodeSnippet(
			"iam_clip::begin(CLIP_ID)\n"
			"    // Float relative to anchor\n"
			"    .key_float_rel(\n"
			"        channel,\n"
			"        time,\n"
			"        0.5f,                      // 50% of anchor\n"
			"        10.0f,                     // +10px offset\n"
			"        iam_anchor_window,         // anchor space\n"
			"        0,                         // axis (0=x, 1=y)\n"
			"        iam_ease_out_cubic         // easing\n"
			"    )\n"
			"    // Vec2 relative: center of window\n"
			"    .key_vec2_rel(\n"
			"        CH_POS, 0.0f,\n"
			"        ImVec2(0.5f, 0.5f),        // 50%, 50%\n"
			"        ImVec2(0, -20),            // offset: 0, -20px\n"
			"        iam_anchor_window\n"
			"    )\n"
			"    .end();\n"
			"\n"
			"// Anchor spaces:\n"
			"iam_anchor_window_content  // GetContentRegionAvail()\n"
			"iam_anchor_window          // GetWindowSize()\n"
			"iam_anchor_viewport        // Viewport size\n"
			"iam_anchor_last_item       // Last item rect size");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Vec4 and Color Relative
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Vec4 and Color Relative"))
	{
		ImGui::TextWrapped(
			"Vec4 relative uses x,y as percentages; z,w remain absolute. "
			"Color relative is for position-based color effects.");

		DocCodeSnippet(
			"// Vec4 relative (x,y are %, z,w are absolute)\n"
			".key_vec4_rel(\n"
			"    channel, time,\n"
			"    ImVec4(0.1f, 0.1f, 0, 0),   // percent (only x,y used)\n"
			"    ImVec4(0, 0, 100, 50),      // px_bias (x,y offset, z,w absolute)\n"
			"    iam_anchor_window\n"
			")\n"
			"\n"
			"// Color relative\n"
			".key_color_rel(\n"
			"    channel, time,\n"
			"    ImVec4(0.5f, 0.5f, 0, 0),   // percent\n"
			"    ImVec4(0, 0, 0, 0),         // offset\n"
			"    iam_col_oklab,              // color space\n"
			"    iam_anchor_viewport\n"
			")");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Interactive Example
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Interactive Example##anchor_keyframes"))
	{
		ImGui::TextWrapped(
			"This circle animates from left to right edge. Click to toggle position!");

		float dt = GetDocDeltaTime();

		// Get canvas dimensions first
		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		float canvas_width = ImMax(ImGui::GetContentRegionAvail().x, 200.0f);
		ImVec2 canvas_size(canvas_width, 100);

		// Animate using simple tweening (percentage-based manually)
		static float target_pct = 0.9f;
		if (ImGui::Button("Toggle Position##anchor_rel")) {
			target_pct = (target_pct > 0.5f) ? 0.1f : 0.9f;
		}

		ImGuiID id = ImGui::GetID("anchor_rel_demo");
		float anim_pct = iam_tween_float(
			id, ImHashStr("pct"),
			target_pct,
			0.8f,  // duration
			iam_ease_preset(iam_ease_out_back),
			iam_policy_crossfade,
			dt
		);

		ImDrawList* dl = ImGui::GetWindowDrawList();

		// Draw background
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw percentage markers with labels
		for (int i = 0; i <= 10; i++) {
			float x = canvas_pos.x + (i / 10.0f) * canvas_size.x;
			dl->AddLine(ImVec2(x, canvas_pos.y + canvas_size.y - 15),
				ImVec2(x, canvas_pos.y + canvas_size.y - 5), IM_COL32(80, 80, 100, 255));
			if (i % 2 == 0) {
				char label[8];
				snprintf(label, sizeof(label), "%d%%", i * 10);
				dl->AddText(ImVec2(x - 10, canvas_pos.y + canvas_size.y - 28),
					IM_COL32(120, 120, 140, 255), label);
			}
		}

		// Draw circle at animated percentage position
		float circle_x = canvas_pos.x + anim_pct * canvas_size.x;
		float circle_y = canvas_pos.y + canvas_size.y * 0.4f;
		dl->AddCircleFilled(ImVec2(circle_x, circle_y), 18.0f, IM_COL32(91, 194, 231, 255));
		dl->AddCircle(ImVec2(circle_x, circle_y), 18.0f, IM_COL32(150, 220, 255, 255), 0, 2.0f);

		// Draw target marker
		float target_x = canvas_pos.x + target_pct * canvas_size.x;
		dl->AddTriangleFilled(
			ImVec2(target_x - 8, canvas_pos.y + 5),
			ImVec2(target_x + 8, canvas_pos.y + 5),
			ImVec2(target_x, canvas_pos.y + 15),
			IM_COL32(255, 180, 100, 200));

		ImGui::Dummy(canvas_size);
		ImGui::Text("Current: %.0f%% | Target: %.0f%% | Canvas: %.0fpx wide",
			anim_pct * 100, target_pct * 100, canvas_size.x);
		ImGui::TextWrapped("Note: Position adapts when window is resized!");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 17: SPRING KEYFRAMES
// ============================================================

static void DocSection_SpringKeyframes()
{
	DocSectionHeader("SPRING KEYFRAMES",
		"Physics-based spring animation for keyframes. The animation overshoots and settles "
		"naturally based on mass, stiffness, and damping parameters.");

	// --------------------------------------------------------
	// Spring Parameters
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Spring Parameters"))
	{
		ImGui::TextWrapped(
			"Configure spring behavior with physics parameters.");

		DocCodeSnippet(
			"iam_spring_params spring;\n"
			"spring.mass = 1.0f;              // Heavier = slower response\n"
			"spring.stiffness = 180.0f;       // Higher = faster, snappier\n"
			"spring.damping = 12.0f;          // Higher = less oscillation\n"
			"spring.initial_velocity = 0.0f;  // Starting velocity\n"
			"\n"
			"iam_clip::begin(CLIP_ID)\n"
			"    .key_float_spring(CH_VALUE, 0.0f, 0.0f, spring)\n"
			"    .key_float_spring(CH_VALUE, 1.0f, 100.0f, spring)\n"
			"    .end();");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Tuning Springs
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Tuning Spring Feel"))
	{
		ImGui::TextWrapped(
			"Different parameter combinations create different animation feels.");

		DocCodeSnippet(
			"// Snappy UI button\n"
			"spring.mass = 1.0f;\n"
			"spring.stiffness = 300.0f;\n"
			"spring.damping = 20.0f;\n"
			"\n"
			"// Soft, floaty\n"
			"spring.mass = 2.0f;\n"
			"spring.stiffness = 80.0f;\n"
			"spring.damping = 8.0f;\n"
			"\n"
			"// Bouncy, playful\n"
			"spring.mass = 1.0f;\n"
			"spring.stiffness = 200.0f;\n"
			"spring.damping = 5.0f;  // Low damping = more bounce\n"
			"\n"
			"// Critically damped (no overshoot)\n"
			"// damping = 2 * sqrt(stiffness * mass)\n"
			"spring.damping = 2.0f * sqrtf(180.0f * 1.0f);  // ~26.8");

		ImGui::Separator();
		ImGui::Text("Interactive Example - Compare Spring Settings:");

		float dt = GetDocDeltaTime();
		static float target = 1.0f;

		if (ImGui::Button("Trigger Springs##spring")) {
			target = (target > 0.5f) ? 0.0f : 1.0f;
		}

		// Three different spring presets
		ImGuiID id = ImGui::GetID("spring_compare");

		// Snappy
		float snappy = iam_tween_float(id, ImHashStr("snappy"), target, 0.8f,
			iam_ease_spring_desc(1.0f, 300.0f, 20.0f, 0.0f), iam_policy_crossfade, dt);

		// Bouncy
		float bouncy = iam_tween_float(id, ImHashStr("bouncy"), target, 0.8f,
			iam_ease_spring_desc(1.0f, 200.0f, 5.0f, 0.0f), iam_policy_crossfade, dt);

		// Floaty
		float floaty = iam_tween_float(id, ImHashStr("floaty"), target, 0.8f,
			iam_ease_spring_desc(2.0f, 80.0f, 8.0f, 0.0f), iam_policy_crossfade, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(320, 110);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw target lines
		float x_start = canvas_pos.x + 20;
		float x_end = canvas_pos.x + 220;
		dl->AddLine(ImVec2(x_start, canvas_pos.y + 15), ImVec2(x_start, canvas_pos.y + 95),
			IM_COL32(60, 60, 80, 255), 1.0f);
		dl->AddLine(ImVec2(x_end, canvas_pos.y + 15), ImVec2(x_end, canvas_pos.y + 95),
			IM_COL32(60, 60, 80, 255), 1.0f);

		// Draw springs with labels on the RIGHT
		float y1 = canvas_pos.y + 30;
		float y2 = canvas_pos.y + 55;
		float y3 = canvas_pos.y + 80;
		float label_x = canvas_pos.x + 240;  // Labels on right side
		float font_half_h = ImGui::GetFontSize() * 0.5f;

		// Snappy (green)
		float x_snappy = x_start + snappy * (x_end - x_start);
		dl->AddCircleFilled(ImVec2(x_snappy, y1), 10.0f, IM_COL32(100, 200, 100, 255));
		dl->AddText(ImVec2(label_x, y1 - font_half_h), IM_COL32(100, 200, 100, 255), "Snappy");

		// Bouncy (yellow)
		float x_bouncy = x_start + bouncy * (x_end - x_start);
		dl->AddCircleFilled(ImVec2(x_bouncy, y2), 10.0f, IM_COL32(255, 200, 100, 255));
		dl->AddText(ImVec2(label_x, y2 - font_half_h), IM_COL32(255, 200, 100, 255), "Bouncy");

		// Floaty (cyan)
		float x_floaty = x_start + floaty * (x_end - x_start);
		dl->AddCircleFilled(ImVec2(x_floaty, y3), 10.0f, IM_COL32(100, 200, 255, 255));
		dl->AddText(ImVec2(label_x, y3 - font_half_h), IM_COL32(100, 200, 255, 255), "Floaty");

		ImGui::Dummy(canvas_size);

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 18: CLIP PERSISTENCE
// ============================================================

static void DocSection_ClipPersistence()
{
	DocSectionHeader("CLIP PERSISTENCE",
		"Save and load clip definitions to/from files. Useful for artist-authored animations "
		"or runtime loading of animation data.");

	// --------------------------------------------------------
	// Save/Load API
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Save and Load Clips"))
	{
		ImGui::TextWrapped(
			"Serialize clip definitions to disk and load them back at runtime.");

		DocCodeSnippet(
			"// Save a clip to file\n"
			"iam_result result = iam_clip_save(CLIP_ID, \"animations/fade_in.ianim\");\n"
			"if (result != iam_ok) {\n"
			"    // Handle error\n"
			"}\n"
			"\n"
			"// Load a clip from file\n"
			"ImGuiID loaded_clip_id;\n"
			"result = iam_clip_load(\"animations/fade_in.ianim\", &loaded_clip_id);\n"
			"if (result == iam_ok) {\n"
			"    // Use loaded_clip_id\n"
			"    iam_play(loaded_clip_id, instance_id);\n"
			"}\n"
			"\n"
			"// Result codes:\n"
			"iam_ok            // Success\n"
			"iam_err_not_found // File not found\n"
			"iam_err_bad_arg   // Invalid arguments\n"
			"iam_err_no_mem    // Memory allocation failed");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Interactive Save/Load Demo
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Interactive Save/Load Demo"))
	{
		ImGui::TextWrapped(
			"Modify the middle keyframe value, save the clip, then change it again. "
			"Load to restore the saved state.");

		static ImGuiID const DOC_PERSIST_CLIP = ImHashStr("doc_persist_clip");
		static ImGuiID const DOC_PERSIST_CH = ImHashStr("doc_persist_ch");
		static ImGuiID const DOC_PERSIST_INST = ImHashStr("doc_persist_inst");
		static char const* PERSIST_FILE_PATH = "persist_demo.ianim";

		static bool persist_clip_init = false;
		static float middle_key_value = 1.0f;
		static float saved_middle_value = 1.0f;
		static bool has_saved = false;
		static bool persist_playing = false;

		auto rebuild_clip = [&]() {
			iam_clip::begin(DOC_PERSIST_CLIP)
				.key_float(DOC_PERSIST_CH, 0.0f, 0.0f, iam_ease_out_cubic)      // Start: 0
				.key_float(DOC_PERSIST_CH, 1.0f, middle_key_value, iam_ease_in_out_cubic) // Middle: editable
				.key_float(DOC_PERSIST_CH, 2.0f, 0.0f, iam_ease_in_cubic)       // End: 0
				.set_loop(true, iam_dir_normal, -1)
				.end();
		};

		if (!persist_clip_init) {
			rebuild_clip();
			persist_clip_init = true;
		}

		ImGui::Separator();
		ImGui::Text("Keyframe Editor:");

		// Slider to modify middle keyframe
		if (ImGui::SliderFloat("Middle Key Value", &middle_key_value, 0.0f, 2.0f, "%.2f")) {
			rebuild_clip();
		}

		ImGui::Spacing();

		// Save button
		if (ImGui::Button("Save")) {
			iam_result result = iam_clip_save(DOC_PERSIST_CLIP, PERSIST_FILE_PATH);
			if (result == iam_ok) {
				saved_middle_value = middle_key_value;
				has_saved = true;
			}
		}
		ImGui::SameLine();

		// Load button (only enabled if we have saved something)
		static iam_result last_load_result = iam_ok;
		ImGui::BeginDisabled(!has_saved);
		if (ImGui::Button("Load")) {
			ImGuiID loaded_id;
			last_load_result = iam_clip_load(PERSIST_FILE_PATH, &loaded_id);
			if (last_load_result == iam_ok) {
				middle_key_value = saved_middle_value;
				// Always restart the animation after load
				iam_play(DOC_PERSIST_CLIP, DOC_PERSIST_INST);
				persist_playing = true;
			}
		}
		ImGui::EndDisabled();

		if (has_saved) {
			ImGui::SameLine();
			ImGui::TextDisabled("(Saved: %.2f)", saved_middle_value);
		}
		if (last_load_result != iam_ok) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Load err: %d", (int)last_load_result);
		}

		// Debug: show clip info
		iam_instance dbg_inst = iam_get_instance(DOC_PERSIST_INST);
		ImGui::Text("Debug: valid=%d playing=%d duration=%.2f time=%.2f",
			dbg_inst.valid() ? 1 : 0,
			dbg_inst.is_playing() ? 1 : 0,
			dbg_inst.duration(),
			dbg_inst.time());

		ImGui::Spacing();
		ImGui::Separator();

		// Play button
		if (ImGui::Button(persist_playing ? "Stop##persist" : "Play##persist")) {
			if (!persist_playing) {
				iam_play(DOC_PERSIST_CLIP, DOC_PERSIST_INST);
				persist_playing = true;
			} else {
				iam_get_instance(DOC_PERSIST_INST).stop();
				persist_playing = false;
			}
		}

		// Visualize the animation
		iam_instance inst = iam_get_instance(DOC_PERSIST_INST);
		float value = 0.0f;
		if (inst.valid()) {
			if (!inst.is_playing()) persist_playing = false;
			inst.get_float(DOC_PERSIST_CH, &value);
			ImGui::SameLine();
			ImGui::Text("Time: %.2f / %.2f playing=%d", inst.time(), inst.duration(), inst.is_playing() ? 1 : 0);
		}

		// Progress bar showing animated value
		ImGui::ProgressBar(value / 2.0f, ImVec2(-1, 20));
		ImGui::Text("Value: %.2f", value);

		// Visual representation
		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		float canvas_w = 300.0f;
		float canvas_h = 100.0f;

		// Background
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_w, canvas_pos.y + canvas_h),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw keyframe visualization
		float k0_x = canvas_pos.x + 30.0f;
		float k1_x = canvas_pos.x + canvas_w * 0.5f;
		float k2_x = canvas_pos.x + canvas_w - 30.0f;
		float base_y = canvas_pos.y + canvas_h - 20.0f;
		float scale = 30.0f;

		// Keyframe markers (diamonds)
		ImU32 key_col = IM_COL32(255, 200, 100, 255);
		float k0_y = base_y;
		float k1_y = base_y - middle_key_value * scale;
		float k2_y = base_y;

		// Lines connecting keyframes
		dl->AddLine(ImVec2(k0_x, k0_y), ImVec2(k1_x, k1_y), IM_COL32(100, 100, 120, 255), 2.0f);
		dl->AddLine(ImVec2(k1_x, k1_y), ImVec2(k2_x, k2_y), IM_COL32(100, 100, 120, 255), 2.0f);

		// Draw keyframe diamonds
		auto draw_diamond = [&](float x, float y, ImU32 col, float size = 6.0f) {
			dl->AddQuadFilled(
				ImVec2(x, y - size), ImVec2(x + size, y),
				ImVec2(x, y + size), ImVec2(x - size, y), col);
		};
		draw_diamond(k0_x, k0_y, key_col);
		draw_diamond(k1_x, k1_y, IM_COL32(100, 255, 150, 255)); // Middle key highlighted
		draw_diamond(k2_x, k2_y, key_col);

		// Draw current position marker
		if (inst.valid() && inst.is_playing()) {
			float t = inst.time() / inst.duration();
			float marker_x = k0_x + t * (k2_x - k0_x);
			float marker_y = base_y - value * scale;
			dl->AddCircleFilled(ImVec2(marker_x, marker_y), 8.0f, IM_COL32(91, 194, 231, 255));
		}

		// Labels
		dl->AddText(ImVec2(k0_x - 8, base_y + 5), IM_COL32(180, 180, 180, 255), "0");
		dl->AddText(ImVec2(k1_x - 8, base_y + 5), IM_COL32(180, 180, 180, 255), "1");
		dl->AddText(ImVec2(k2_x - 8, base_y + 5), IM_COL32(180, 180, 180, 255), "2");

		ImGui::Dummy(ImVec2(canvas_w, canvas_h));

		iam_show_debug_timeline(DOC_PERSIST_INST);

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 19: UTILITY FUNCTIONS
// ============================================================

static void DocSection_UtilityFunctions()
{
	DocSectionHeader("UTILITY FUNCTIONS",
		"Standalone helper functions for color blending, easing evaluation, "
		"anchor size queries, and clip information.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Color Blending
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_get_blended_color"))
	{
		ImGui::TextWrapped(
			"Blend two sRGB colors in any color space. Useful for custom color calculations.");

		DocCodeSnippet(
			"ImVec4 result = iam_get_blended_color(\n"
			"    color_a,      // First color (sRGB)\n"
			"    color_b,      // Second color (sRGB)\n"
			"    t,            // Blend factor [0,1]\n"
			"    iam_col_oklab // Color space for blending\n"
			");");

		ImGui::Separator();

		static float blend_t = 0.5f;
		static int blend_space = iam_col_oklab;
		ImGui::SliderFloat("Blend##blend", &blend_t, 0.0f, 1.0f);
		char const* spaces[] = { "sRGB", "Linear", "HSV", "OKLAB", "OKLCH" };
		ImGui::Combo("Color Space##blend", &blend_space, spaces, 5);

		ImVec4 a(1, 0, 0, 1);  // Red
		ImVec4 b(0, 0, 1, 1);  // Blue
		ImVec4 result = iam_get_blended_color(a, b, blend_t, blend_space);

		ImGui::ColorButton("A##blend", a, 0, ImVec2(40, 20));
		ImGui::SameLine();
		ImGui::Text("->");
		ImGui::SameLine();
		ImGui::ColorButton("Result##blend", result, 0, ImVec2(80, 20));
		ImGui::SameLine();
		ImGui::Text("<-");
		ImGui::SameLine();
		ImGui::ColorButton("B##blend", b, 0, ImVec2(40, 20));

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Anchor Size
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_anchor_size"))
	{
		ImGui::TextWrapped(
			"Get the current size of an anchor space for layout calculations.");

		DocCodeSnippet(
			"ImVec2 size = iam_anchor_size(iam_anchor_window);\n"
			"\n"
			"// Anchor spaces:\n"
			"iam_anchor_window_content  // Content region\n"
			"iam_anchor_window          // Window size\n"
			"iam_anchor_viewport        // Viewport size\n"
			"iam_anchor_last_item       // Last item size");

		ImGui::Separator();
		ImGui::Text("Current anchor sizes:");
		ImVec2 content = iam_anchor_size(iam_anchor_window_content);
		ImVec2 window = iam_anchor_size(iam_anchor_window);
		ImGui::BulletText("Content: %.0f x %.0f", content.x, content.y);
		ImGui::BulletText("Window: %.0f x %.0f", window.x, window.y);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Easing Evaluation
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_eval_preset"))
	{
		ImGui::TextWrapped(
			"Evaluate a preset easing function at any t value. Useful for custom calculations.");

		DocCodeSnippet(
			"// Evaluate easing at t [0,1]\n"
			"float eased = iam_eval_preset(iam_ease_out_cubic, t);\n"
			"\n"
			"// Example: Manual interpolation\n"
			"float from = 0.0f, to = 100.0f;\n"
			"float t = 0.5f;\n"
			"float eased_t = iam_eval_preset(iam_ease_out_elastic, t);\n"
			"float value = from + (to - from) * eased_t;");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Clip Info
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Clip Information"))
	{
		ImGui::TextWrapped(
			"Query information about registered clips.");

		DocCodeSnippet(
			"// Check if clip exists\n"
			"if (iam_clip_exists(CLIP_ID)) {\n"
			"    // Clip is registered\n"
			"}\n"
			"\n"
			"// Get clip duration (computed from keyframes)\n"
			"float duration = iam_clip_duration(CLIP_ID);\n"
			"\n"
			"// Get stagger delay for a specific index\n"
			"float delay = iam_stagger_delay(CLIP_ID, index);");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// LUT Samples
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_set_ease_lut_samples"))
	{
		ImGui::TextWrapped(
			"Configure the resolution of lookup tables used for parametric easings "
			"(cubic bezier, spring). Higher values = more accuracy, more memory.");

		DocCodeSnippet(
			"// Set LUT resolution (default: 256)\n"
			"iam_set_ease_lut_samples(512);  // Higher accuracy\n"
			"iam_set_ease_lut_samples(128);  // Lower memory");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 20: SMOOTH NOISE
// ============================================================

static void DocSection_SmoothNoise()
{
	DocSectionHeader("SMOOTH NOISE",
		"Simple, smooth random movement using noise. Easier to use than full noise channels, "
		"perfect for subtle organic animation.");

	float dt = GetDocDeltaTime();

	// --------------------------------------------------------
	// Smooth Noise API
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Smooth Noise Functions"))
	{
		ImGui::TextWrapped(
			"Convenience functions for smooth random movement without configuring noise options.");

		DocCodeSnippet(
			"// 1D smooth noise\n"
			"float offset = iam_smooth_noise_float(\n"
			"    id,\n"
			"    amplitude,  // Range of movement\n"
			"    speed,      // How fast it changes\n"
			"    dt\n"
			");\n"
			"\n"
			"// 2D smooth noise\n"
			"ImVec2 offset = iam_smooth_noise_vec2(\n"
			"    id,\n"
			"    ImVec2(10, 10),  // Amplitude per axis\n"
			"    2.0f,            // Speed\n"
			"    dt\n"
			");\n"
			"\n"
			"// 4D and color versions also available\n"
			"ImVec4 offset4 = iam_smooth_noise_vec4(id, amplitude4, speed, dt);\n"
			"ImVec4 color = iam_smooth_noise_color(\n"
			"    id, base_color, amplitude, speed, iam_col_oklab, dt\n"
			");");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Interactive Example
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Interactive Example##smooth_noise"))
	{
		static float amplitude = 20.0f;
		static float speed = 1.5f;

		ImGui::SliderFloat("Amplitude##noise", &amplitude, 5.0f, 50.0f);
		ImGui::SliderFloat("Speed##noise", &speed, 0.5f, 5.0f);

		ImGuiID id = ImGui::GetID("smooth_noise_demo");
		ImVec2 offset = iam_smooth_noise_vec2(id, ImVec2(amplitude, amplitude), speed, dt);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + 200, canvas_pos.y + 100),
			IM_COL32(30, 30, 40, 255), 4.0f);

		ImVec2 center(canvas_pos.x + 100 + offset.x, canvas_pos.y + 50 + offset.y);
		dl->AddCircleFilled(center, 15.0f, IM_COL32(91, 194, 231, 255));

		ImGui::Dummy(ImVec2(200, 100));
		ImGui::Text("Offset: (%.1f, %.1f)", offset.x, offset.y);

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 21: PATH MORPHING TWEEN
// ============================================================

static void DocSection_PathMorphingTween()
{
	DocSectionHeader("PATH MORPHING TWEEN",
		"Animate both position along a path AND the morph blend between two paths simultaneously. "
		"Creates complex shape-shifting motion effects.");

	// --------------------------------------------------------
	// Path Morph Tween
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_tween_path_morph"))
	{
		ImGui::TextWrapped(
			"Animate along a morphing path with separate easing for path progress and morph blend.");

		DocCodeSnippet(
			"ImVec2 pos = iam_tween_path_morph(\n"
			"    id, channel_id,\n"
			"    PATH_A, PATH_B,      // Two paths to morph between\n"
			"    target_blend,        // Target morph blend [0,1]\n"
			"    duration,\n"
			"    path_ease,           // Easing for position along path\n"
			"    morph_ease,          // Easing for morph transition\n"
			"    policy,\n"
			"    dt,\n"
			"    opts                 // iam_morph_opts (optional)\n"
			");\n"
			"\n"
			"// Query current morph blend value\n"
			"float current_blend = iam_get_morph_blend(id, channel_id);");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Morph Options
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Morph Options"))
	{
		ImGui::TextWrapped(
			"Configure how paths are resampled and blended.");

		DocCodeSnippet(
			"iam_morph_opts opts;\n"
			"opts.samples = 64;          // Resample resolution\n"
			"opts.match_endpoints = true; // Force endpoints to match\n"
			"opts.use_arc_length = true;  // Use arc-length for smoothness\n"
			"\n"
			"// Use in morphing functions\n"
			"ImVec2 pos = iam_path_morph(PATH_A, PATH_B, t, blend, opts);\n"
			"ImVec2 tangent = iam_path_morph_tangent(PATH_A, PATH_B, t, blend, opts);\n"
			"float angle = iam_path_morph_angle(PATH_A, PATH_B, t, blend, opts);");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Interactive Example
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Interactive Example##path_morphing"))
	{
		ImGui::TextWrapped(
			"Morph between a circle and a square path while animating position.");

		// Create two paths
		static ImGuiID const DOC_PATH_MORPH_A = ImHashStr("doc_path_morph_a");
		static ImGuiID const DOC_PATH_MORPH_B = ImHashStr("doc_path_morph_b");
		static bool morph_paths_init = false;
		if (!morph_paths_init) {
			// Path A: Circle (approximated with bezier curves)
			float cx = 100, cy = 50, r = 35;
			float k = r * 0.552284749831f;  // Bezier approximation constant
			iam_path::begin(DOC_PATH_MORPH_A, ImVec2(cx + r, cy))
				.cubic_to(ImVec2(cx + r, cy + k), ImVec2(cx + k, cy + r), ImVec2(cx, cy + r))
				.cubic_to(ImVec2(cx - k, cy + r), ImVec2(cx - r, cy + k), ImVec2(cx - r, cy))
				.cubic_to(ImVec2(cx - r, cy - k), ImVec2(cx - k, cy - r), ImVec2(cx, cy - r))
				.cubic_to(ImVec2(cx + k, cy - r), ImVec2(cx + r, cy - k), ImVec2(cx + r, cy))
				.end();

			// Path B: Square
			float s = 30;
			iam_path::begin(DOC_PATH_MORPH_B, ImVec2(cx + s, cy - s))
				.line_to(ImVec2(cx + s, cy + s))
				.line_to(ImVec2(cx - s, cy + s))
				.line_to(ImVec2(cx - s, cy - s))
				.close()
				.end();

			iam_path_build_arc_lut(DOC_PATH_MORPH_A, 64);
			iam_path_build_arc_lut(DOC_PATH_MORPH_B, 64);
			morph_paths_init = true;
		}

		static float morph_blend = 0.0f;
		ImGui::SliderFloat("Circle <-> Square##morph", &morph_blend, 0.0f, 1.0f);

		float dt = GetDocDeltaTime();
		static float path_t = 0.0f;
		path_t += dt * 0.4f;
		if (path_t > 1.0f) path_t -= 1.0f;

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(200, 100);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw morphed path
		iam_morph_opts opts;
		opts.samples = 64;
		opts.use_arc_length = true;

		ImVec2 prev = iam_path_morph(DOC_PATH_MORPH_A, DOC_PATH_MORPH_B, 0.0f, morph_blend, opts);
		for (int i = 1; i <= 50; i++) {
			float t = (float)i / 50.0f;
			ImVec2 curr = iam_path_morph(DOC_PATH_MORPH_A, DOC_PATH_MORPH_B, t, morph_blend, opts);
			dl->AddLine(
				ImVec2(canvas_pos.x + prev.x, canvas_pos.y + prev.y),
				ImVec2(canvas_pos.x + curr.x, canvas_pos.y + curr.y),
				IM_COL32(80, 80, 120, 255), 2.0f);
			prev = curr;
		}

		// Draw animated dot
		ImVec2 pos = iam_path_morph(DOC_PATH_MORPH_A, DOC_PATH_MORPH_B, path_t, morph_blend, opts);
		dl->AddCircleFilled(ImVec2(canvas_pos.x + pos.x, canvas_pos.y + pos.y), 8.0f,
			IM_COL32(91, 194, 231, 255));

		ImGui::Dummy(canvas_size);

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 22: CURVE FUNCTIONS
// ============================================================

static void DocSection_CurveFunctions()
{
	DocSectionHeader("CURVE FUNCTIONS",
		"Standalone curve evaluation functions for direct use without creating paths. "
		"Useful for one-off calculations or custom rendering.");

	// --------------------------------------------------------
	// Bezier Curves
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Bezier Curves"))
	{
		ImGui::TextWrapped(
			"Evaluate quadratic and cubic Bezier curves at any parameter t.");

		DocCodeSnippet(
			"// Quadratic Bezier (3 control points)\n"
			"ImVec2 pos = iam_bezier_quadratic(p0, p1, p2, t);\n"
			"\n"
			"// Cubic Bezier (4 control points)\n"
			"ImVec2 pos = iam_bezier_cubic(p0, p1, p2, p3, t);\n"
			"\n"
			"// Derivatives (for tangent/velocity)\n"
			"ImVec2 velocity = iam_bezier_quadratic_deriv(p0, p1, p2, t);\n"
			"ImVec2 velocity = iam_bezier_cubic_deriv(p0, p1, p2, p3, t);");

		ImGui::Separator();
		ImGui::Text("Interactive Example (cubic bezier):");

		float dt = GetDocDeltaTime();
		static float bezier_t = 0.0f;
		bezier_t += dt * 0.4f;
		if (bezier_t > 1.0f) bezier_t -= 1.0f;

		// Control points
		ImVec2 p0(20, 70), p1(60, 15), p2(140, 85), p3(180, 30);

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(200, 100);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw control polygon
		dl->AddLine(ImVec2(canvas_pos.x + p0.x, canvas_pos.y + p0.y),
			ImVec2(canvas_pos.x + p1.x, canvas_pos.y + p1.y), IM_COL32(60, 60, 80, 255), 1.0f);
		dl->AddLine(ImVec2(canvas_pos.x + p2.x, canvas_pos.y + p2.y),
			ImVec2(canvas_pos.x + p3.x, canvas_pos.y + p3.y), IM_COL32(60, 60, 80, 255), 1.0f);

		// Draw curve
		ImVec2 prev = iam_bezier_cubic(p0, p1, p2, p3, 0.0f);
		for (int i = 1; i <= 40; i++) {
			float t = (float)i / 40.0f;
			ImVec2 curr = iam_bezier_cubic(p0, p1, p2, p3, t);
			dl->AddLine(ImVec2(canvas_pos.x + prev.x, canvas_pos.y + prev.y),
				ImVec2(canvas_pos.x + curr.x, canvas_pos.y + curr.y),
				IM_COL32(91, 194, 231, 255), 2.0f);
			prev = curr;
		}

		// Draw control points
		dl->AddCircleFilled(ImVec2(canvas_pos.x + p0.x, canvas_pos.y + p0.y), 4.0f, IM_COL32(255, 100, 100, 200));
		dl->AddCircleFilled(ImVec2(canvas_pos.x + p1.x, canvas_pos.y + p1.y), 4.0f, IM_COL32(255, 200, 100, 200));
		dl->AddCircleFilled(ImVec2(canvas_pos.x + p2.x, canvas_pos.y + p2.y), 4.0f, IM_COL32(100, 255, 100, 200));
		dl->AddCircleFilled(ImVec2(canvas_pos.x + p3.x, canvas_pos.y + p3.y), 4.0f, IM_COL32(100, 100, 255, 200));

		// Draw animated point
		ImVec2 pos = iam_bezier_cubic(p0, p1, p2, p3, bezier_t);
		dl->AddCircleFilled(ImVec2(canvas_pos.x + pos.x, canvas_pos.y + pos.y), 6.0f,
			IM_COL32(255, 255, 100, 255));

		ImGui::Dummy(canvas_size);
		ImGui::Text("t = %.2f", bezier_t);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Catmull-Rom Splines
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Catmull-Rom Splines"))
	{
		ImGui::TextWrapped(
			"Catmull-Rom splines pass through the middle control points (p1, p2). "
			"p0 and p3 influence the curve shape but aren't on the curve.");

		DocCodeSnippet(
			"// Catmull-Rom spline (passes through p1 and p2)\n"
			"ImVec2 pos = iam_catmull_rom(\n"
			"    p0, p1, p2, p3,\n"
			"    t,              // Parameter [0,1] interpolates p1 to p2\n"
			"    tension         // 0.5 = standard, 0 = loose, 1 = tight\n"
			");\n"
			"\n"
			"// Derivative\n"
			"ImVec2 tangent = iam_catmull_rom_deriv(p0, p1, p2, p3, t, tension);");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Interactive Example
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Interactive Example##curve_functions"))
	{
		ImGui::TextWrapped(
			"Cubic Bezier with draggable control points and tangent visualization.");

		// Control points
		static ImVec2 p0(20, 80), p1(60, 20), p2(140, 80), p3(180, 30);

		float dt = GetDocDeltaTime();
		static float curve_t = 0.0f;
		curve_t += dt * 0.5f;
		if (curve_t > 1.0f) curve_t -= 1.0f;

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(200, 100);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		// Draw control polygon
		dl->AddLine(ImVec2(canvas_pos.x + p0.x, canvas_pos.y + p0.y),
			ImVec2(canvas_pos.x + p1.x, canvas_pos.y + p1.y), IM_COL32(60, 60, 80, 255), 1.0f);
		dl->AddLine(ImVec2(canvas_pos.x + p1.x, canvas_pos.y + p1.y),
			ImVec2(canvas_pos.x + p2.x, canvas_pos.y + p2.y), IM_COL32(60, 60, 80, 255), 1.0f);
		dl->AddLine(ImVec2(canvas_pos.x + p2.x, canvas_pos.y + p2.y),
			ImVec2(canvas_pos.x + p3.x, canvas_pos.y + p3.y), IM_COL32(60, 60, 80, 255), 1.0f);

		// Draw control points
		dl->AddCircleFilled(ImVec2(canvas_pos.x + p0.x, canvas_pos.y + p0.y), 4.0f, IM_COL32(255, 100, 100, 200));
		dl->AddCircleFilled(ImVec2(canvas_pos.x + p1.x, canvas_pos.y + p1.y), 4.0f, IM_COL32(255, 200, 100, 200));
		dl->AddCircleFilled(ImVec2(canvas_pos.x + p2.x, canvas_pos.y + p2.y), 4.0f, IM_COL32(100, 255, 100, 200));
		dl->AddCircleFilled(ImVec2(canvas_pos.x + p3.x, canvas_pos.y + p3.y), 4.0f, IM_COL32(100, 100, 255, 200));

		// Draw curve
		ImVec2 prev = iam_bezier_cubic(p0, p1, p2, p3, 0.0f);
		for (int i = 1; i <= 50; i++) {
			float t = (float)i / 50.0f;
			ImVec2 curr = iam_bezier_cubic(p0, p1, p2, p3, t);
			dl->AddLine(ImVec2(canvas_pos.x + prev.x, canvas_pos.y + prev.y),
				ImVec2(canvas_pos.x + curr.x, canvas_pos.y + curr.y),
				IM_COL32(91, 194, 231, 255), 2.0f);
			prev = curr;
		}

		// Draw animated point and tangent
		ImVec2 pos = iam_bezier_cubic(p0, p1, p2, p3, curve_t);
		ImVec2 deriv = iam_bezier_cubic_deriv(p0, p1, p2, p3, curve_t);

		// Normalize tangent and scale
		float len = sqrtf(deriv.x * deriv.x + deriv.y * deriv.y);
		if (len > 0.001f) {
			deriv.x = deriv.x / len * 25;
			deriv.y = deriv.y / len * 25;
		}

		dl->AddCircleFilled(ImVec2(canvas_pos.x + pos.x, canvas_pos.y + pos.y), 6.0f,
			IM_COL32(255, 255, 100, 255));
		dl->AddLine(ImVec2(canvas_pos.x + pos.x, canvas_pos.y + pos.y),
			ImVec2(canvas_pos.x + pos.x + deriv.x, canvas_pos.y + pos.y + deriv.y),
			IM_COL32(255, 100, 100, 255), 2.0f);

		ImGui::Dummy(canvas_size);

		ImGui::Text("Control points: P0(red) P1(orange) P2(green) P3(blue)");
		ImGui::Text("Yellow dot with tangent arrow");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 23: QUAD TRANSFORMS
// ============================================================

static void DocSection_QuadTransforms()
{
	DocSectionHeader("QUAD TRANSFORMS",
		"Transform quad vertices for rotated sprites, text glyphs, or custom rendering. "
		"Used internally for text-on-path but exposed for custom use.");

	// --------------------------------------------------------
	// Transform Quad
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_transform_quad"))
	{
		ImGui::TextWrapped(
			"Transform an array of 4 vertices (quad) by rotation and translation.");

		DocCodeSnippet(
			"// Quad vertices (4 corners)\n"
			"ImVec2 quad[4] = {\n"
			"    ImVec2(0, 0),      // Top-left\n"
			"    ImVec2(50, 0),     // Top-right\n"
			"    ImVec2(50, 20),    // Bottom-right\n"
			"    ImVec2(0, 20)      // Bottom-left\n"
			"};\n"
			"\n"
			"// Transform in place\n"
			"iam_transform_quad(\n"
			"    quad,\n"
			"    ImVec2(25, 10),    // Center of rotation\n"
			"    angle_radians,\n"
			"    ImVec2(100, 50)    // Translation\n"
			");");

		ImGui::Separator();
		ImGui::Text("Interactive Example:");

		float dt = GetDocDeltaTime();
		static float tq_angle = 0.0f;
		tq_angle += dt * 1.5f;
		if (tq_angle > IM_PI * 2.0f) tq_angle -= IM_PI * 2.0f;

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(180, 80);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255), 4.0f);

		ImVec2 quad[4] = {
			ImVec2(-25, -12),
			ImVec2(25, -12),
			ImVec2(25, 12),
			ImVec2(-25, 12)
		};
		ImVec2 center(canvas_pos.x + 90, canvas_pos.y + 40);
		iam_transform_quad(quad, ImVec2(0, 0), tq_angle, center);

		dl->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], IM_COL32(91, 194, 231, 200));
		dl->AddQuad(quad[0], quad[1], quad[2], quad[3], IM_COL32(120, 220, 255, 255), 2.0f);

		ImGui::Dummy(canvas_size);

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Make Glyph Quad
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_make_glyph_quad"))
	{
		ImGui::TextWrapped(
			"Create a rotated quad for a text glyph positioned on a path.");

		DocCodeSnippet(
			"ImVec2 quad[4];\n"
			"\n"
			"iam_make_glyph_quad(\n"
			"    quad,\n"
			"    pos,               // Position on path\n"
			"    angle_radians,     // Rotation (from path tangent)\n"
			"    glyph_width,\n"
			"    glyph_height,\n"
			"    baseline_offset    // Vertical offset for baseline\n"
			");\n"
			"\n"
			"// Use with ImDrawList\n"
			"ImDrawList* dl = ImGui::GetWindowDrawList();\n"
			"dl->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], color);");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Visual Example: Rotating Quads
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Visual Example: Rotating Quads"))
	{
		ImGui::TextWrapped(
			"Interactive demo showing quad transformation with rotation and translation.");

		static float quad_angle = 0.0f;
		static bool auto_rotate = true;

		ImGui::Checkbox("Auto Rotate", &auto_rotate);
		if (!auto_rotate)
		{
			ImGui::SliderFloat("Angle", &quad_angle, 0.0f, IM_PI * 2.0f);
		}
		else
		{
			quad_angle += ImGui::GetIO().DeltaTime * 2.0f;
			if (quad_angle > IM_PI * 2.0f) quad_angle -= IM_PI * 2.0f;
		}

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(300, 150);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		// Background
		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255));

		ImVec2 center1(canvas_pos.x + 80, canvas_pos.y + 75);
		ImVec2 center2(canvas_pos.x + 220, canvas_pos.y + 75);

		// Quad 1: using iam_transform_quad
		{
			ImVec2 quad[4] = {
				ImVec2(-30, -15),
				ImVec2(30, -15),
				ImVec2(30, 15),
				ImVec2(-30, 15)
			};
			iam_transform_quad(quad, ImVec2(0, 0), quad_angle, center1);
			dl->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], IM_COL32(100, 150, 255, 200));
			dl->AddQuad(quad[0], quad[1], quad[2], quad[3], IM_COL32(150, 200, 255, 255), 2.0f);
		}

		// Quad 2: using iam_make_glyph_quad (simulating glyph)
		{
			ImVec2 quad[4];
			iam_make_glyph_quad(quad, center2, quad_angle + IM_PI * 0.25f, 50.0f, 30.0f, 0.0f);
			dl->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], IM_COL32(255, 150, 100, 200));
			dl->AddQuad(quad[0], quad[1], quad[2], quad[3], IM_COL32(255, 200, 150, 255), 2.0f);
		}

		// Labels
		ImVec2 label1(center1.x - 40, canvas_pos.y + canvas_size.y - 20);
		ImVec2 label2(center2.x - 50, canvas_pos.y + canvas_size.y - 20);
		dl->AddText(label1, IM_COL32(200, 200, 200, 255), "transform_quad");
		dl->AddText(label2, IM_COL32(200, 200, 200, 255), "make_glyph_quad");

		ImGui::Dummy(canvas_size);
		ImGui::Text("Blue: iam_transform_quad | Orange: iam_make_glyph_quad");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 24: TEXT SIZING HELPERS
// ============================================================

static void DocSection_TextSizing()
{
	DocSectionHeader("TEXT SIZING HELPERS",
		"Calculate text dimensions for layout planning before rendering.");

	// --------------------------------------------------------
	// Text Path Width
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_text_path_width"))
	{
		ImGui::TextWrapped(
			"Get the total width of text as it would be rendered along a path.");

		DocCodeSnippet(
			"iam_text_path_opts opts;\n"
			"opts.letter_spacing = 2.0f;\n"
			"opts.font_scale = 1.5f;\n"
			"\n"
			"float width = iam_text_path_width(\"Hello World\", opts);\n"
			"\n"
			"// Use for centering or checking if text fits\n"
			"float path_len = iam_path_length(PATH_ID);\n"
			"if (width > path_len) {\n"
			"    // Text is too long for path\n"
			"}");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Text Stagger Sizing
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("iam_text_stagger_width/duration"))
	{
		ImGui::TextWrapped(
			"Get width and animation duration for staggered text.");

		DocCodeSnippet(
			"iam_text_stagger_opts opts;\n"
			"opts.char_delay = 0.05f;\n"
			"opts.char_duration = 0.3f;\n"
			"opts.letter_spacing = 2.0f;\n"
			"\n"
			"// Total width for layout\n"
			"float width = iam_text_stagger_width(\"Hello\", opts);\n"
			"\n"
			"// Total animation duration\n"
			"// (accounts for stagger delays + last char animation)\n"
			"float duration = iam_text_stagger_duration(\"Hello\", opts);\n"
			"// For 5 chars: 4 * 0.05 + 0.3 = 0.5 seconds");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Visual Example: Text Sizing Demo
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Visual Example: Text Sizing Demo"))
	{
		ImGui::TextWrapped(
			"Shows how text sizing helpers calculate dimensions for layout planning.");

		static char test_text[64] = "Hello World";
		static float letter_spacing = 2.0f;
		static float font_scale = 1.0f;
		static float char_delay = 0.05f;
		static float char_duration = 0.3f;

		ImGui::InputText("Text", test_text, IM_ARRAYSIZE(test_text));
		ImGui::SliderFloat("Letter Spacing", &letter_spacing, 0.0f, 10.0f);
		ImGui::SliderFloat("Font Scale", &font_scale, 0.5f, 2.0f);

		ImGui::Separator();
		ImGui::Text("Stagger Animation Settings:");
		ImGui::SliderFloat("Char Delay", &char_delay, 0.01f, 0.2f);
		ImGui::SliderFloat("Char Duration", &char_duration, 0.1f, 1.0f);

		// Calculate sizes
		iam_text_path_opts path_opts;
		path_opts.letter_spacing = letter_spacing;
		path_opts.font_scale = font_scale;
		float path_width = iam_text_path_width(test_text, path_opts);

		iam_text_stagger_opts stagger_opts;
		stagger_opts.char_delay = char_delay;
		stagger_opts.char_duration = char_duration;
		stagger_opts.letter_spacing = letter_spacing;
		float stagger_width = iam_text_stagger_width(test_text, stagger_opts);
		float stagger_duration = iam_text_stagger_duration(test_text, stagger_opts);

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Calculated Values:");
		ImGui::BulletText("Path Text Width: %.1f px", path_width);
		ImGui::BulletText("Stagger Text Width: %.1f px", stagger_width);
		ImGui::BulletText("Stagger Duration: %.2f sec", stagger_duration);

		// Visual width bar
		ImVec2 bar_pos = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		float max_bar = 300.0f;
		float bar_w = ImMin(path_width, max_bar);
		dl->AddRectFilled(bar_pos, ImVec2(bar_pos.x + bar_w, bar_pos.y + 20), IM_COL32(100, 180, 255, 200));
		dl->AddRect(bar_pos, ImVec2(bar_pos.x + max_bar, bar_pos.y + 20), IM_COL32(100, 100, 100, 255));
		ImGui::Dummy(ImVec2(max_bar, 24));
		ImGui::Text("Width visualization (max 300px shown)");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 25: GRADIENT HELPERS
// ============================================================

static void DocSection_GradientHelpers()
{
	DocSectionHeader("GRADIENT HELPERS",
		"Factory methods for creating common gradient types quickly.");

	// --------------------------------------------------------
	// Gradient Factory Methods
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Gradient Factory Methods"))
	{
		ImGui::TextWrapped(
			"Quick constructors for common gradient patterns.");

		DocCodeSnippet(
			"// Solid color (single stop)\n"
			"iam_gradient solid = iam_gradient::solid(ImVec4(1, 0, 0, 1));\n"
			"\n"
			"// Two-color gradient\n"
			"iam_gradient two = iam_gradient::two_color(\n"
			"    ImVec4(1, 0, 0, 1),  // Start (red)\n"
			"    ImVec4(0, 0, 1, 1)   // End (blue)\n"
			");\n"
			"\n"
			"// Three-color gradient (start, middle, end)\n"
			"iam_gradient three = iam_gradient::three_color(\n"
			"    ImVec4(1, 0, 0, 1),  // Start (red)\n"
			"    ImVec4(1, 1, 0, 1),  // Middle (yellow)\n"
			"    ImVec4(0, 1, 0, 1)   // End (green)\n"
			");\n"
			"\n"
			"// Custom gradient with any number of stops\n"
			"iam_gradient custom;\n"
			"custom.add(0.0f, red);\n"
			"custom.add(0.25f, orange);\n"
			"custom.add(0.5f, yellow);\n"
			"custom.add(0.75f, green);\n"
			"custom.add(1.0f, blue);\n"
			"\n"
			"// Sample gradient\n"
			"ImVec4 color = custom.sample(0.5f, iam_col_oklab);");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Visual Example: Gradient Gallery
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Visual Example: Gradient Gallery"))
	{
		ImGui::TextWrapped(
			"Visual comparison of different gradient factory methods and color spaces.");

		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 bar_size(280, 25);
		float spacing = 30.0f;
		ImDrawList* dl = ImGui::GetWindowDrawList();

		// Create gradients
		iam_gradient solid = iam_gradient::solid(ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
		iam_gradient two = iam_gradient::two_color(
			ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
			ImVec4(0.2f, 0.2f, 1.0f, 1.0f));
		iam_gradient three = iam_gradient::three_color(
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
			ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
		iam_gradient rainbow;
		rainbow.add(0.0f, ImVec4(1, 0, 0, 1));
		rainbow.add(0.2f, ImVec4(1, 0.5f, 0, 1));
		rainbow.add(0.4f, ImVec4(1, 1, 0, 1));
		rainbow.add(0.6f, ImVec4(0, 1, 0, 1));
		rainbow.add(0.8f, ImVec4(0, 0.5f, 1, 1));
		rainbow.add(1.0f, ImVec4(0.5f, 0, 1, 1));

		auto draw_gradient_bar = [&](ImVec2 pos, const char* label, iam_gradient& grad, int color_space)
		{
			for (int i = 0; i < (int)bar_size.x; i++)
			{
				float t = (float)i / bar_size.x;
				ImVec4 col = grad.sample(t, color_space);
				ImU32 c = ImGui::ColorConvertFloat4ToU32(col);
				dl->AddLine(ImVec2(pos.x + i, pos.y), ImVec2(pos.x + i, pos.y + bar_size.y), c);
			}
			dl->AddRect(pos, ImVec2(pos.x + bar_size.x, pos.y + bar_size.y), IM_COL32(100, 100, 100, 255));
			dl->AddText(ImVec2(pos.x + bar_size.x + 10, pos.y + 4), IM_COL32(200, 200, 200, 255), label);
		};

		ImVec2 pos = canvas_pos;
		draw_gradient_bar(pos, "Solid", solid, iam_col_srgb); pos.y += spacing;
		draw_gradient_bar(pos, "Two-Color (sRGB)", two, iam_col_srgb); pos.y += spacing;
		draw_gradient_bar(pos, "Two-Color (OkLab)", two, iam_col_oklab); pos.y += spacing;
		draw_gradient_bar(pos, "Three-Color", three, iam_col_oklab); pos.y += spacing;
		draw_gradient_bar(pos, "Rainbow", rainbow, iam_col_oklab);

		ImGui::Dummy(ImVec2(400, spacing * 5 + 10));
		ImGui::Text("Note: OkLab produces perceptually uniform color transitions");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 26: STYLE HELPERS
// ============================================================

static void DocSection_StyleHelpers()
{
	DocSectionHeader("STYLE HELPERS",
		"Additional functions for managing registered styles.");

	// --------------------------------------------------------
	// Style Management
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Style Management"))
	{
		ImGui::TextWrapped(
			"Register, query, and remove style snapshots.");

		DocCodeSnippet(
			"// Register current ImGui style under an ID\n"
			"ImGui::StyleColorsDark();\n"
			"iam_style_register_current(STYLE_DARK);\n"
			"\n"
			"ImGui::StyleColorsLight();\n"
			"iam_style_register_current(STYLE_LIGHT);\n"
			"\n"
			"// Check if a style is registered\n"
			"if (iam_style_exists(STYLE_DARK)) {\n"
			"    // Style is available\n"
			"}\n"
			"\n"
			"// Remove a registered style\n"
			"iam_style_unregister(STYLE_OLD);\n"
			"\n"
			"// Register explicit style object\n"
			"ImGuiStyle custom_style = ...;\n"
			"iam_style_register(STYLE_CUSTOM, custom_style);");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 27: TRANSFORM MATRIX FUNCTIONS
// ============================================================

static void DocSection_TransformMatrix()
{
	DocSectionHeader("TRANSFORM MATRIX FUNCTIONS",
		"Convert between iam_transform and 3x2 transformation matrices for "
		"integration with other graphics systems.");

	// --------------------------------------------------------
	// Matrix Conversion
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Matrix Conversion"))
	{
		ImGui::TextWrapped(
			"Convert transforms to/from 3x2 matrices (row-major format).");

		DocCodeSnippet(
			"// Decompose a 3x2 matrix into transform\n"
			"// Matrix format: [m00 m01 tx; m10 m11 ty]\n"
			"iam_transform t = iam_transform_from_matrix(\n"
			"    m00, m01,  // First row (scale/rotation)\n"
			"    m10, m11,  // Second row (scale/rotation)\n"
			"    tx, ty     // Translation\n"
			");\n"
			"\n"
			"// Convert transform to 3x2 matrix\n"
			"float matrix[6];  // Row-major: m00, m01, tx, m10, m11, ty\n"
			"iam_transform_to_matrix(transform, matrix);\n"
			"\n"
			"// Use with other graphics systems\n"
			"// e.g., canvas.setTransform(m00, m10, m01, m11, tx, ty);");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Visual Example: Matrix Visualization
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Visual Example: Matrix Visualization"))
	{
		ImGui::TextWrapped(
			"Interactive demo showing transform-to-matrix conversion with animated parameters.");

		static float mat_angle = 0.0f;
		static float mat_scale = 1.0f;
		static bool auto_animate = true;

		ImGui::Checkbox("Auto Animate", &auto_animate);
		if (auto_animate)
		{
			mat_angle += ImGui::GetIO().DeltaTime * 1.5f;
			mat_scale = 0.7f + 0.3f * ImSin(mat_angle * 0.5f);
		}
		else
		{
			ImGui::SliderFloat("Rotation", &mat_angle, 0.0f, IM_PI * 2.0f);
			ImGui::SliderFloat("Scale", &mat_scale, 0.3f, 2.0f);
		}

		// Create transform and convert to matrix
		iam_transform tf;
		tf.position = ImVec2(100, 75);
		tf.rotation = mat_angle;
		tf.scale = ImVec2(mat_scale, mat_scale);

		float matrix[6];
		iam_transform_to_matrix(tf, matrix);

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Transform:");
		ImGui::BulletText("Position: (%.1f, %.1f)", tf.position.x, tf.position.y);
		ImGui::BulletText("Rotation: %.2f rad (%.0f deg)", tf.rotation, tf.rotation * 180.0f / IM_PI);
		ImGui::BulletText("Scale: (%.2f, %.2f)", tf.scale.x, tf.scale.y);

		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "3x2 Matrix:");
		ImGui::Text("| %+.3f  %+.3f  %+.1f |", matrix[0], matrix[1], matrix[2]);
		ImGui::Text("| %+.3f  %+.3f  %+.1f |", matrix[3], matrix[4], matrix[5]);

		// Visual representation
		ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size(200, 150);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
			IM_COL32(30, 30, 40, 255));

		ImVec2 center(canvas_pos.x + 100, canvas_pos.y + 75);

		// Draw original unit square (before transform)
		float half = 30.0f;
		dl->AddRect(ImVec2(center.x - half, center.y - half),
			ImVec2(center.x + half, center.y + half),
			IM_COL32(100, 100, 100, 128), 0.0f, 0, 1.0f);

		// Draw transformed square
		ImVec2 corners[4] = {
			ImVec2(-half, -half), ImVec2(half, -half),
			ImVec2(half, half), ImVec2(-half, half)
		};

		for (int i = 0; i < 4; i++)
		{
			float sx = corners[i].x * mat_scale;
			float sy = corners[i].y * mat_scale;
			float rx = sx * ImCos(mat_angle) - sy * ImSin(mat_angle);
			float ry = sx * ImSin(mat_angle) + sy * ImCos(mat_angle);
			corners[i] = ImVec2(center.x + rx, center.y + ry);
		}

		dl->AddQuadFilled(corners[0], corners[1], corners[2], corners[3], IM_COL32(100, 200, 255, 150));
		dl->AddQuad(corners[0], corners[1], corners[2], corners[3], IM_COL32(150, 220, 255, 255), 2.0f);

		ImGui::Dummy(canvas_size);
		ImGui::Text("Gray: original | Blue: transformed");

		ImGui::TreePop();
	}
}

// ============================================================
// SECTION 28: DEBUG TOOLS
// ============================================================

static void DocSection_DebugTools()
{
	DocSectionHeader("DEBUG TOOLS",
		"Visual debugging and inspection tools for animations.");

	// --------------------------------------------------------
	// Debug Timeline
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Debug Timeline"))
	{
		ImGui::TextWrapped(
			"Visual timeline showing clip tracks, keyframes, and playhead position. "
			"Hover keyframes to see values.");

		DocCodeSnippet(
			"// Show timeline for a clip instance\n"
			"iam_show_debug_timeline(instance_id);");

		ImGui::TreePop();
	}

	// --------------------------------------------------------
	// Unified Inspector
	// --------------------------------------------------------
	DocApplyOpenAll();
	if (ImGui::TreeNode("Unified Inspector"))
	{
		ImGui::TextWrapped(
			"Comprehensive debug window showing all active animations, pools, and system state.");

		DocCodeSnippet(
			"static bool show_inspector = true;\n"
			"iam_show_unified_inspector(&show_inspector);");

		ImGui::Separator();

		static bool show = false;
		ImGui::Checkbox("Show Inspector", &show);
		if (show) {
			iam_show_unified_inspector(&show);
		}

		ImGui::TreePop();
	}
}

// ============================================================
// MAIN DOCUMENTATION WINDOW
// ============================================================

void ImAnimDocWindow()
{
	// Update animation systems
	iam_update_begin_frame();
	iam_clip_update(GetDocDeltaTime());

	if (!ImGui::Begin("ImAnim Documentation"))
	{
		ImGui::End();
		return;
	}

	// Header
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.9f, 1.0f));
	ImGui::Text("ImAnim - Animation Library for Dear ImGui");
	ImGui::PopStyleColor();
	ImGui::TextWrapped(
		"Complete documentation with interactive examples. Click on sections to expand.");

	// Open/Close all sections
	if (ImGui::Button("Open All")) {
		s_doc_open_all = 1;
	}
	ImGui::SameLine();
	if (ImGui::Button("Close All")) {
		s_doc_open_all = -1;
	}

	ImGui::Separator();

	// Sections
	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("1. Tween API - Value Types"))
		DocSection_TweenTypes();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("2. Tween API - Policies"))
		DocSection_TweenPolicies();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("3. Easing Functions"))
		DocSection_Easing();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("4. Color Spaces"))
		DocSection_ColorSpaces();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("5. Clip System (Timeline Animation)"))
		DocSection_ClipSystem();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("6. Motion Paths"))
		DocSection_MotionPaths();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("7. Procedural Animation"))
		DocSection_Procedural();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("8. Text Animation"))
		DocSection_TextAnimation();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("9. Advanced Features"))
		DocSection_Advanced();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("10. Frame Management & Optimization"))
		DocSection_FrameManagement();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("11. Scroll Animation"))
		DocSection_ScrollAnimation();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("12. Per-Axis Easing"))
		DocSection_PerAxisEasing();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("13. Arc-Length Parameterization"))
		DocSection_ArcLength();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("14. Animation Layering"))
		DocSection_Layering();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("15. Clip Callbacks"))
		DocSection_ClipCallbacks();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("16. Anchor-Relative Keyframes"))
		DocSection_AnchorRelativeKeyframes();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("17. Spring Keyframes"))
		DocSection_SpringKeyframes();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("18. Clip Persistence"))
		DocSection_ClipPersistence();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("19. Utility Functions"))
		DocSection_UtilityFunctions();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("20. Smooth Noise"))
		DocSection_SmoothNoise();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("21. Path Morphing Tween"))
		DocSection_PathMorphingTween();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("22. Curve Functions"))
		DocSection_CurveFunctions();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("23. Quad Transforms"))
		DocSection_QuadTransforms();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("24. Text Sizing Helpers"))
		DocSection_TextSizing();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("25. Gradient Helpers"))
		DocSection_GradientHelpers();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("26. Style Helpers"))
		DocSection_StyleHelpers();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("27. Transform Matrix Functions"))
		DocSection_TransformMatrix();

	DocApplyOpenAll();
	if (ImGui::CollapsingHeader("28. Debug Tools"))
		DocSection_DebugTools();

	// Reset open all flag
	s_doc_open_all = 0;

	ImGui::End();
}
