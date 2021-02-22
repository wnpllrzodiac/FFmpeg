// texture rotate
// https://gamedev.stackexchange.com/questions/98723/texture-rotation-inside-a-quad
// https://godotengine.org/qa/41400/simple-texture-rotation-shader

// https://www.shadertoy.com/new
// fragColor = texture(iChannel0, pos);

mat2 Rot(float a) {
    float s=sin(a), c=cos(a);
    return mat2(c, -s, s, c);
}

const float PI = 3.141592653;
const float bias_angle = PI/3.0; // PI/2.0, PI/3.0
const float bias_offset = 0.5;  // 0.55,    0.5

uniform int u_type; // 0-origin, 1-six, 2-four-diag, 3-four,4-updown,5-leftright,6-frameinframe

void main(){    
    vec2 pos = vec2(texCoord.x, 1.0 - texCoord.y);
    vec2 p = 2.0 * pos - vec2(1.0);
    float a = atan(p.x,p.y); // [-PI, PI]
    if (a < -3.0 * PI / 4.0 || a >= 3.0 * PI / 4.0) { // down
        pos = Rot(PI) * (pos - 0.5) * 0.6 + 0.5;
    } else if (a < -PI / 4.0 && a >= -PI / 2.0) { // left-up
        vec2 rotate = (pos - 0.5) * (0.6 / sin(bias_angle)) * Rot(-PI/3.0);
        vec2 fix_square = vec2(rotate.x, rotate.y * u_screenSize.x / u_screenSize.y);
        pos = fix_square + bias_offset;
    } else if (a < -PI / 2.0 && a >= -3.0 * PI / 4.0) { // left-down
        vec2 rotate = (pos - 0.5) * (0.6 / sin(bias_angle)) * Rot(-2.0 * PI/3.0);
        vec2 fix_square = vec2(rotate.x, rotate.y * u_screenSize.x / u_screenSize.y);
        pos = fix_square + bias_offset;
    } else if (a < PI / 4.0 && a >= -PI / 4.0) { //  up
        pos = (pos - 0.5) * 0.6 + 0.5;
    } else if (a < PI / 2.0 && a >= PI / 4.0) { // right-up
        vec2 rotate = (pos - 0.5) * (0.6 / sin(bias_angle)) * Rot(PI/3.0);
        vec2 fix_square = vec2(rotate.x, rotate.y * u_screenSize.x / u_screenSize.y);
        pos = fix_square + bias_offset;
    }
    else if (a < 3.0 * PI / 4.0 && a >= PI / 2.0) { // right-down
        vec2 rotate = (pos - 0.5) * (0.6 / sin(bias_angle)) * Rot(2.0 * PI/3.0);
        vec2 fix_square = vec2(rotate.x, rotate.y * u_screenSize.x / u_screenSize.y);
        pos = fix_square + bias_offset;
    }
    
    pos.y = 1.0 - pos.y;
    
    gl_FragColor = texture2D(tex, pos);
}