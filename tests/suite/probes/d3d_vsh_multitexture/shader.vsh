xvs.1.1

; RCC kept live through oFog forces CXBX's CPU vertex path. The probe then
; verifies that four-component coordinates for stages 0 and 3 survive that
; path while the translated pixel shader remains active.
rcc r0.x, v0.w
mov oPos, v0
mov oFog.x, r0.x
mov oD0, v1
mov oT0, v2
mov oT3, v3
