void main(){
    vec2 pos = texCoord.xy;
    if (pos.x <= 0.5 && pos.y<= 0.5){ //左上
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
    }
    gl_FragColor = texture2D(tex, pos);
}