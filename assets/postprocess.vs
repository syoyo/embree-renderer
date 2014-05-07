uniform float eye_offset_x;
varying vec2 oTexCoord;

void main()
{
   gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
   // 0.8 = aspect ratio
   oTexCoord = vec2(gl_MultiTexCoord0.x, gl_MultiTexCoord0.y);
   oTexCoord.y = (1.0-oTexCoord.y);
}
