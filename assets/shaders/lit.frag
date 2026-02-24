#version 460 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

// Material
uniform sampler2D textureSampler;
uniform float shininess;          // 32.0 is a reasonable default

// Directional light
uniform vec3 dirLightDir;         // Direction the light points
uniform vec3 dirLightColor;       // Color/intensity
uniform float dirLightAmbient;    // Ambient strength
uniform bool hasDirLight;         // Is there a directional light?

// point lights - supports up to MAX_POINT_LIGHTS simultaneously
#define MAX_POINT_LIGHTS 8

struct PointLightData
{
	vec3 position;
	vec3 color;
	float ambient;
	float linear;
	float quadratic;
};

uniform int numPointLights;
uniform PointLightData pointLights[MAX_POINT_LIGHTS];
uniform vec4 colorOverride;

// Camera
uniform vec3 viewPos;             // Camera position (for specular)

// ─── Helper: calculate one point light's contribution ──────────
vec3 calcPointLight
(
	PointLightData light,
	vec3 normal,
	vec3 fragPos,
	vec3 viewDir,
	vec3 texColor
)
{
	vec3 lDir = normalize(light.position - fragPos);

	float distance = length(light.position - fragPos);
	float attenuation = 1.0 / (1.0 + light.linear * distance + light.quadratic * distance * distance);

	vec3 ambient = light.ambient * light.color;

	float diff = max(dot(normal, lDir), 0.0);
	vec3 diffuse = diff * light.color;

	vec3 reflectDir = reflect(-lDir, normal);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
	vec3 specular = spec * light.color;

	return (ambient + (diffuse + specular) * attenuation) * texColor;
}


void main() {
    vec3 texColor = texture(textureSampler, TexCoord).rgb;
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);

    vec3 result = vec3(0.0);

    // ─── Directional light contribution ──────────────────────────
    if (hasDirLight) {
        vec3 lDir = normalize(-dirLightDir);

        vec3 ambient = dirLightAmbient * dirLightColor;

        float diff = max(dot(norm, lDir), 0.0);
        vec3 diffuse = diff * dirLightColor;

        vec3 reflectDir = reflect(-lDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
        vec3 specular = spec * dirLightColor;

        result += (ambient + diffuse + specular) * texColor;
    }

    // ─── Point light contribution ────────────────────────────────
    for (int i = 0; i < numPointLights; i++)
	{
		result += calcPointLight(pointLights[i], norm, FragPos, viewDir, texColor);
	}

	if (colorOverride.a > 0.0)
		FragColor = colorOverride;
	else
		FragColor = vec4(result, 1.0);
}