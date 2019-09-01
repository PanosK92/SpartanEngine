static const uint g_ssgi_steps 				= 32;
static const float g_ssgi_step_scale	= 0.001f;

float3 SSGI(Light light, float3 pos, float3 normal, float2 uv)
{
	// Convert everything to view space
	float3 view_pos			= mul(float4(pos, 1.0f), g_view).xyz;
	float3 view_normal		= normalize(mul(float4(normal, 0.0f), g_view).xyz);
	float3 view_reflection 	= normalize(reflect(view_normal, light.direction));

	// Compute starting ray
	float3 ray_pos			= view_pos;
	float3 ray_step			= view_reflection * g_ssgi_step_scale;
	float2 ray_uv 			= 0.0f;

	float3 color = 0.0f;
	for (int i = 0; i < g_ssgi_steps; i++)
	{
		// Step ray
		ray_pos += ray_step;
		ray_uv 	= project(ray_pos, g_projection);

		// Compare depth
		float depth_sampled = get_linear_depth(tex_depth, samplerLinear_clamp, ray_uv);
		float depth_delta 	= ray_pos.z - depth_sampled;

		if (depth_delta > 0.0f)
		{
			color += light.color * light.intensity * 0.1f;
			break;
		}
	}

	return color;
}