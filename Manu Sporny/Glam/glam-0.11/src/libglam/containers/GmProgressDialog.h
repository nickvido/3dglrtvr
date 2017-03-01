/*
 * Copyright (c) 2002-2004, xRhino, Inc.
 *
 * This library is free software; you can redistribute and/or modify
 * it under the terms of the dual license distributed with this source
 * package (a combined Trolltech QPL/GPL-compatible license)
 *
 * Under this dual license scheme, a separate license is available for use of 
 * this code with a proprietary (closed) system. For more information on the 
 * license, please see the LICENSE file distributed with this source package.
 */
#ifndef GMPROGRESSDIALOG_H
#define GMPROGRESSDIALOG_H

#include <glam/GmDialog.h>

/**
 * GmProgressDialog is supposed to be a dialog with a bar meter to
 * show progress in loading or computing some data.
 * This class is not yet implemented.
 */
class GmProgressDialog : public GmDialog
{
 public:
   /**
    * Standard constructor
    */
   GmProgressDialog();

   /**
    * Virtual destructor
    */
   virtual ~GmProgressDialog();
};

#endif