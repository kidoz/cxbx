// ******************************************************************
// * 
// * proj : openLIBC
// *
// * desc : Totally Free LIC replacement
// *
// * file : fmod.c
// *
// * note : This LIBC is TOTALLY free - do what you like with it!!
// *
// ******************************************************************

#include <xlibc/math.h>

#ifdef _MSC_VER


float fmodf(float f, float g)
{
	float temp1,temp2;
	__asm
	{
		fld g
		fld f
		fprem
		fstp temp2
		fstp temp1
	}
	return temp2;
}

double fmod(double f, double g)
{
	double temp1,temp2;
	__asm
	{
		fld g
		fld f
		fprem
		fstp temp2
		fstp temp1
	}
	return temp2;
}
#else

OPENXDK_UNIMPLEMENTEDC(fmod)

#endif 
