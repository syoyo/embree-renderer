// -> From OculusSDK ----------------------------------------------------------
//
//  Licensed under Apache 2.0 License.

uniform vec2 LensCenter;
uniform vec2 ScreenCenter;
uniform float XCenterOffset;

// XWidth + XMargin = 1.0
uniform float XWidth;            // <= 1.0
uniform float XMargin;           // <= 1.0

uniform vec2 Scale;
uniform vec2 ScaleIn;
uniform vec4 HmdWarpParam;
uniform vec4 ChromAbParam;
uniform sampler2D Texture0;
varying vec2 oTexCoord;

// Scales input texture coordinates for distortion.
// ScaleIn maps texture coordinates to Scales to ([-1, 1]), although top/bottom will be
// larger due to aspect ratio.
void main()
{
   vec2 sc = ScreenCenter;   
   vec2 lc = LensCenter;
   float tx_offset = 0.0;

   float regionScale = 1.0;

    // No distortion.
    //{
    //    gl_FragColor = texture2D(Texture0, oTexCoord);
    //    return;
    //}

   if (oTexCoord.x >= 0.5) { // right
       sc.x += 0.5;
       lc.x = 0.5 + (0.5 - XCenterOffset * 0.5)*0.5;
       tx_offset = 2.0*XMargin;
   }

   vec2  theta = (oTexCoord - lc) * ScaleIn; // Scales to [-1, 1]
   float rSq= theta.x * theta.x + theta.y * theta.y;
   vec2  theta1 = theta * (HmdWarpParam.x + HmdWarpParam.y * rSq + 
                  HmdWarpParam.z * rSq * rSq + HmdWarpParam.w * rSq * rSq * rSq);
 
   //float tss = rSq < 0.2 ? 1.0 : 0.0;
   //gl_FragColor = vec4(oTexCoord.xy, 0.0, 1.0);
   //return;

   // Detect whether blue texture coordinates are out of range since these will scaled out the furthest.
   vec2 thetaBlue = theta1 * (ChromAbParam.z + ChromAbParam.w * rSq);
   vec2 tcBlue = lc + Scale * thetaBlue;

   //gl_FragColor = vec4(float(int(oTexCoord.y * 10.0)) / 10.0, 0.0, 0.0, 1.0);
   //gl_FragColor = vec4(tcBlue, 0.0, 1.0);
   //return;

   if (!all(equal(clamp(tcBlue, sc-vec2(0.25*regionScale,0.5*regionScale), sc+vec2(0.25*regionScale,0.5*regionScale)), tcBlue)))
   {
       gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
       return;
   }
   //gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
   //return;

   //float ss = oTexCoord.y > 0.8 ? 1.0 : 0.0;
   //gl_FragColor = vec4(ss, ss, 0.0, 1.0);
   //return;

   // Now do blue texture lookup.
   tcBlue.x *= 2.0 * XWidth;
   tcBlue.x += tx_offset;
   float blue = texture2D(Texture0, tcBlue).b;

   // Do green lookup (no scaling).
   vec2  tcGreen = lc + Scale * theta1;
   tcGreen.x *= 2.0 * XWidth;
   tcGreen.x += tx_offset;
   vec4  center = texture2D(Texture0, tcGreen);

   // Do red scale and lookup.
   vec2  thetaRed = theta1 * (ChromAbParam.x + ChromAbParam.y * rSq);
   vec2  tcRed = lc + Scale * thetaRed;
   tcRed.x *= 2.0 * XWidth;
   tcRed.x += tx_offset;
   float red = texture2D(Texture0, tcRed).r;

   gl_FragColor = vec4(red, center.g, blue, center.a);
}
