#include <stdio.h>

ungetc(c, iop)
register FILE *iop;
{
	if (c == EOF)
		return(-1);
	if ((iop->_flag&_IOREAD)==0 || iop->_ptr <= iop->_base)
		if (iop->_ptr == iop->_base && iop->_cnt==0)
			*iop->_ptr++;
		else
			return(-1);
	iop->_cnt++;
	if (iop->_flag & _IOSTRG)	/* toyos: sscanf's string may be a read-only */
		iop->_ptr--;		/* literal; c is already there anyway */
	else
		*--iop->_ptr = c;
	return(0);
}
