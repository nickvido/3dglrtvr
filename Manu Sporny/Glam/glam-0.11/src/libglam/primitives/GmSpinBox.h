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
#ifndef GMSPINBOX_H
#define GMSPINBOX_H

#include <glam/GmInteractiveWidget.h>

/**
 * This class is not yet implemented.
 */
class GmSpinBox: public GmInteractiveWidget
{
 protected:
   // Description
   int mValue;
   
 public:
   /**
    * Standard constructor
    */
   GmSpinBox();

   /**
    * Standard destructor
    */
   virtual ~GmSpinBox();
   
   /**
    * Description
    */
   void setValue(unsigned int value);
   
   /**
    * Description
    */
   unsigned int getValue();
};

#endif