/***
    Despill - for cleaning up keying residue.
    Written by unfa for Olive Video Editor.
***/

#version 440
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float factorIn;
    float balanceIn;
    int channel;
};
layout(binding = 2) uniform sampler2D tex;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const vec2 renderScale = vec2(1.0, 1.0);

float average(float a, float b, float balance)
{
    return float ( ( (a * ( 1.0 + balance)) + (b * (1.0 - balance)) ) * 0.5 );
}

float composite(float a, float b, float factor)
{
    return (( min(a, b) * factor ) + (a * ( 1.0 - factor)));
}

void main(void)
{

    float factor  = factorIn  * 0.01; // convert factor from 0..100 scale to 0..1 scale
    float balance = balanceIn * 0.01; // convert balance from -100..100 scale to -1..1 scale

    vec2 uv = gl_FragCoord.xy/resolution.xy;

    vec4 col = texture(tex, uv);

    // separate channels

    float r = col.r;
    float g = col.g;
    float b = col.b;
    float a = col.a;

    // Select despilled channel: 0 = RED; 1 = GREEN; 2 = BLUE;

    if (channel == 0) // RED
    {
        // replace new channel by avaraging between existing channels
        float r2 = average(g, b, balance);

        // now composite the original and new channel using the "darken" mode
        r = composite(r, r2, factor);
    }

    if (channel == 1) // GREEN
    {
        float g2 = average(r, b, balance);

        g = composite(g, g2, factor);
    }

    if (channel == 2) // BLUE
    {
        float b2 = average(r, g, balance);

        b = composite(b, b2, factor);
    }

    // return the result
    fragColor = vec4(r, g, b, a);
}
