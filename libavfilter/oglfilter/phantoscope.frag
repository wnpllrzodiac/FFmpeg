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

uniform int u_type; // 0-origin, 1-six, 2-four-diag, 3-four,4-updown,5-leftright,6-frameinframe

void main(){
    //vec2 pos = Rot(PI/2.0) * (texCoord.xy - 0.5) + 0.5;
    /*if (pos.x <= 0.5 && pos.y<= 0.5){ //左上
        pos.x = pos.x * 2.0;
        pos.y = pos.y * 2.0;
    } else if (pos.x > 0.5 && pos.y< 0.5){ //右上
        pos.x = 1.0 - (pos.x - 0.5) * 2.0;
        pos.y = pos.y * 2.0;
    } else if (pos.y> 0.5 && pos.x < 0.5) { //左下
        pos.y = 1.0 - (pos.y - 0.5) * 2.0;
        pos.x = pos.x * 2.0;
    } else if (pos.y> 0.5 && pos.x > 0.5){ //右下
        pos.y = 1.0 - (pos.y - 0.5) * 2.0;
        pos.x = 1.0 - (pos.x - 0.5) * 2.0;
    }*/
    
    vec2 pos = texCoord.xy;
    if (u_type == 4) {
        pos = vec2(texCoord.x, 1.0 - texCoord.y);
        // pos = (pos.xy - 0.5) / 2.0 + 0.5; // zoom in 2x
        if (pos.y >= 0.5) { // up
            pos.y = pos.y - 0.25;
        }
        else { // down
            pos.y = 1.0 - pos.y - 0.25;
        }
        
        pos.y = 1.0 - pos.y;
    }
    else if (u_type == 5) {
        if (pos.x >= 0.5) {
            pos.x = 1.0 - pos.x;
        }
    }
    else if (u_type == 3) {
        pos = vec2(texCoord.x, 1.0 - texCoord.y);
        if (pos.x <= 0.5 && pos.y >= 0.5){ //左上
            pos.x = pos.x + 0.25;
            pos.y = pos.y - 0.25;
        } else if (pos.x > 0.5 && pos.y >= 0.5){ //右上
            pos.x = 1.0 - pos.x + 0.25;
            pos.y = pos.y - 0.25;
        } else if (pos.x <= 0.5 && pos.y < 0.5) { //左下
            pos.x = pos.x + 0.25;
            pos.y = 1.0 - pos.y - 0.25;
        } else if (pos.x > 0.5 && pos.y < 0.5){ //右下
            pos.x = 1.0 - pos.x + 0.25;
            pos.y = 1.0 - pos.y - 0.25;
        }
        
        pos.y = 1.0 - pos.y;
    }
    else if (u_type == 2) {
        pos = vec2(texCoord.x, 1.0 - texCoord.y);
        vec2 p = 2.0 * pos - vec2(1.0);
        float a = atan(p.x,p.y); // [-PI, PI]
        if (a < -3.0 * PI / 4.0 || a >= 3.0 * PI / 4.0) { // down
            pos = Rot(PI) * (pos - 0.5) * u_screenSize.x / u_screenSize.y + 0.5;
            pos.y -= 0.25;
        } else if (a < -PI / 4.0 && a >= -3.0 * PI / 4.0) { // left 
            vec2 rotate = Rot(PI/2.0) * (pos - 0.5);
            vec2 fix_square = vec2(rotate.x, rotate.y * u_screenSize.x / u_screenSize.y);
            pos = fix_square + 0.5;
            pos.y -= 0.25;
        } else if (a < PI / 4.0 && a >= -PI / 4.0) { //  up
            pos = (pos - 0.5) * u_screenSize.x / u_screenSize.y + 0.5;
            pos.y -= 0.25;
        } else if (a < 3.0 * PI / 4.0 && a >= PI / 4.0) { // right
            vec2 rotate = Rot(-PI/2.0) * (pos - 0.5);
            vec2 fix_square = vec2(rotate.x, rotate.y * u_screenSize.x / u_screenSize.y);
            pos = fix_square + 0.5;
            pos.y -= 0.25;
        }
        
        pos.y = 1.0 - pos.y;
    }
    else if (u_type == 1) {
        pos = vec2(texCoord.x, 1.0 - texCoord.y);
        vec2 p = 2.0 * pos - vec2(1.0);
        float a = atan(p.x,p.y); // [-PI, PI]
        if (a < -3.0 * PI / 4.0 || a >= 3.0 * PI / 4.0) { // down
            pos = Rot(PI) * (pos - 0.5) * u_screenSize.x / u_screenSize.y + 0.5;
            pos.y -= 0.25;
        } else if (a < -PI / 4.0 && a >= -PI / 2.0) { // left-up
            vec2 rotate = Rot(PI/3.0) * (pos - 0.5);
            vec2 fix_square = vec2(rotate.x, rotate.y * u_screenSize.x / u_screenSize.y);
            pos = fix_square + 0.5;
            pos.y -= 0.25;
        } else if (a < -PI / 2.0 && a >= -3.0 * PI / 4.0) { // left-down
            vec2 rotate = Rot(2.0 * PI/3.0) * (pos - 0.5);
            vec2 fix_square = vec2(rotate.x, rotate.y * u_screenSize.x / u_screenSize.y);
            pos = fix_square + 0.5;
            pos.y -= 0.25;
        } else if (a < PI / 4.0 && a >= -PI / 4.0) { //  up
            pos = (pos - 0.5) * u_screenSize.x / u_screenSize.y + 0.5;
            pos.y -= 0.25;
        } else if (a < PI / 2.0 && a >= PI / 4.0) { // right-up
            vec2 rotate = Rot(-PI/3.0) * (pos - 0.5);
            vec2 fix_square = vec2(rotate.x, rotate.y * u_screenSize.x / u_screenSize.y);
            pos = fix_square + 0.5;
            pos.y -= 0.25;
        }
        else if (a < 3.0 * PI / 4.0 && a >= PI / 2.0) { // right-down
            vec2 rotate = Rot(-2.0 * PI/3.0) * (pos - 0.5);
            vec2 fix_square = vec2(rotate.x, rotate.y * u_screenSize.x / u_screenSize.y);
            pos = fix_square + 0.5;
            pos.y -= 0.25;
        }
        
        pos.y = 1.0 - pos.y;
    }
    
    gl_FragColor = texture2D(tex, pos);
}