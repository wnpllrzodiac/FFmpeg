mat2 Rot(float a) {
    float s=sin(a), c=cos(a);
    return mat2(c, -s, s, c);
}

uniform int u_type; // 0-origin, 1-six, 2-four-diag, 3-four,4-updown,5-leftright,6-frameinframe

void main(){
    //vec2 pos = Rot(3.1415926/2.0) * texCoord.xy;
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
    gl_FragColor = texture2D(tex, pos);
}