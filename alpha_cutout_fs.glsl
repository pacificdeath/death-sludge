#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;
uniform float damageFlash;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord) * fragColor;

    if (texel.a < 0.1) {
        discard;
    }

    finalColor = vec4(mix(texel.rgb, vec3(1.0), damageFlash), 1.0);
}
