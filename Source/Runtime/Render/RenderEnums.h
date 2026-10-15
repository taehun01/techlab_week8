#pragma once

enum class EShaderType
{
	Vertex,
	Pixel,
	Compute,
};

enum class ELightingModel : int32 { Gouraud, Lambert, BlinnPhong };