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

// Point light
uniform vec3 pointLightPos;       // World-space position
uniform vec3 pointLightColor;     // Color/intensity
uniform float pointLightAmbient;  // Ambient strength
uniform bool hasPointLight;       // Is there a point light?

// Camera
uniform vec3 viewPos;             // Camera position (for specular)

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
    if (hasPointLight) {
        vec3 lDir = normalize(pointLightPos - FragPos);

        float distance = length(pointLightPos - FragPos);
        float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);

        vec3 ambient = pointLightAmbient * pointLightColor;

        float diff = max(dot(norm, lDir), 0.0);
        vec3 diffuse = diff * pointLightColor;

        vec3 reflectDir = reflect(-lDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
        vec3 specular = spec * pointLightColor;

        result += (ambient + (diffuse + specular) * attenuation) * texColor;
    }

    FragColor = vec4(result, 1.0);
}