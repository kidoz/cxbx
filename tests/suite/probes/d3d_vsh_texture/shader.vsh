xvs.1.1

; Keeping RCC live through oFog forces CXBX's CPU path because the host shader
; cannot reproduce its clamp semantics. Fog is disabled by the probe.
rcc r0.x, v0.w
mov oPos, v0
mov oFog.x, r0.x
mov oD0, v1
mov oT0, v2
