// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->core->Error.h
// *
// *  This file is part of the cxbx project.
// *
// *  cxbx and cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file LICENSE.md.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#ifndef ERROR_H
#define ERROR_H

#include "Cxbx.h"

// ******************************************************************
// * Inherited from this class for easy Error detection
// ******************************************************************
class Error
{
    public:
        // ******************************************************************
        // * Return current error (or zero if there is none)
        // ******************************************************************
        const char *GetError() const { return m_szError; }

        // ******************************************************************
        // * Is the current error fatal? (if so, class should be 'dead')
        // ******************************************************************
        bool IsFatal() const { return m_bFatal; }

        // ******************************************************************
        // * Clear current error (returns false if error was fatal)
        // ******************************************************************
        bool ClearError();

    protected:
        // ******************************************************************
        // * Protected constructor so this class must be inherited from
        // ******************************************************************
        Error() : m_szError(0), m_bFatal(false) { }

        // ******************************************************************
        // * Protected deconstructor
        // ******************************************************************
       ~Error() { delete[] m_szError; }

        // ******************************************************************
        // * Protected so only derived class may set an error
        // ******************************************************************
        void SetError(const char *x_szError, bool x_bFatal);

    private:
        // ******************************************************************
        // * Current error information
        // ******************************************************************
        bool  m_bFatal;
        char *m_szError;
};

#endif
