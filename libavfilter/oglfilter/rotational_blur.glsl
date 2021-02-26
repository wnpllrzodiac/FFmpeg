#define PI 3.1415926

float easeInOutCubic(float x)
{
	return x < 0.5 ? 2.0 * x * x : 1.0 - pow( -2.0 * x + 2.0, 3.0 ) / 2.0;
}

vec2 rotate(vec2 videoImageCoord,vec2 centerImageCoord,float radianAngle)
{
    vec2 rotateCenter = centerImageCoord;
    float rotateAngle = radianAngle ;
    float cos=cos(rotateAngle);
    float sin=sin(rotateAngle);      
    mat3 rotateMat=mat3(cos,-sin,0.0,
                        sin,cos,0.0,
                        0.0,0.0,1.0);
    vec3 deltaOffset;
    deltaOffset = rotateMat*vec3(videoImageCoord.x- rotateCenter.x,videoImageCoord.y- rotateCenter.y,1.0);
    videoImageCoord.x = deltaOffset.x+rotateCenter.x;
    videoImageCoord.y = deltaOffset.y+rotateCenter.y;
    return videoImageCoord;
}

vec3 rotateBlur(sampler2D tex,vec2 center,vec2 resolution,vec2 curCoord,float intensity)
{
	vec2 dxy = curCoord - center;
	float r = length(dxy);
	float angle = atan(dxy.y,dxy.x);
	int num = 15;

	vec3 color = vec3(0.0);
	float step = 0.01;
	for(int i = 0; i < num; i++)
	{
	   angle += (step * intensity);
	   
	   float  newX = r*cos(angle) + center.x;  
	   float  newY = r*sin(angle) + center.y;  
	   newX = abs(newX);
	   if(newX > resolution.x)
	   		newX = resolution.x - mod(newX,resolution.x);
	   newY = abs(newY);
	   if(newY > resolution.y)
	   		newY = resolution.y - mod(newY,resolution.y);	   

	   color += texture2D(tex, vec2(newX, newY)/resolution).rgb;
	}
	color /= float(num);	
	return color;
}

vec4 transition(vec2 texCoord)
{
    vec2 textureCoordinate = vec2(texCoord.x, 1.0 - texCoord.y);
	vec2 resolution = u_screenSize;
	vec2 rotateCenter = resolution * 0.5;
	vec2 realCoord = textureCoordinate * resolution;

	float t = progress * 1.01;
	// float bezier = pow((1.0 - t),3.0) * 0.25 + 3.0 * t * pow(1.0 - t,2.0) * 0.1 + 
	// 				3.0 * t * t * (1.0 - t) * 0.25 + pow(t,3.0);
	float bezier = easeInOutCubic(t);


	vec3 resultColor = vec3(0.0);
	realCoord = rotate(realCoord,rotateCenter,bezier * PI * 2.0);
	if(t <= 0.5)
	{
		resultColor = rotateBlur(from,rotateCenter,resolution,realCoord,t * 2.0);
	}
	else if(t > 0.5 && t <= 1.0)
	{
		resultColor = rotateBlur(to,rotateCenter,resolution,realCoord,(1.0 - t) * 2.0);
	}
	else
	{
		resultColor = texture2D(to,textureCoordinate).rgb;
	}

	return vec4(resultColor,1.0);
}
