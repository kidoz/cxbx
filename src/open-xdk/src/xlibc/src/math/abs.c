// ******************************************************************
// * 
// * proj : openLIBC
// *
// * desc : Totally Free LIC replacement
// *
// * file : abs.c
// *
// * note : This LIBC is TOTALLY free - do what you like with it!!
// *
// ******************************************************************

#include	<xlibc/ansidecl.h>


// Return the absolute value of I. 
int	abs( int i )
{
	if( i>=0 ) 
		return i; 
	else 
		return -i;
}

