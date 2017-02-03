/*
	Raytracer
	Copyright (c) 2004-2005, Trenkwalder Markus
	All rights reserved. 
	
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions
	are met:
	
	- Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	  
	- Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.
	  
	- Neither the name of the library's copyright owner nor the names
	  of its contributors may be used to endorse or promote products
	  derived from this software without specific prior written permission.
	  
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
	A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
	OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
	TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
	PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
	LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
	NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
	
	Contact info:
	email: trenki2@gmx.net
*/

#include "pinholecamera.h"

#include <cmath>

using namespace math3d;

PinholeCamera::PinholeCamera(const mat4& m, float fovy, float aspect, unsigned long raytype) :
	ICamera(m, raytype),
	location(transformPointToWorld(vec3(0.0f, 0.0f, 0.0f))),
	ex(transformVectorToWorld(vec3(1.0f * aspect, 0.0f, 0.0f))),
	ey(transformVectorToWorld(vec3(0.0f, 1.0f, 0.0f))),
	ez(transformVectorToWorld(vec3(0.0f, 0.0f, 1.0f))),
	image_plane_distance(1.0f / std::tan(fovy / 2.0f))
{
}

Ray PinholeCamera::generateRay(const vec2& ndc) const {
	return Ray(
		location,
		normalized(
			vec3(
				ndc[0] * ex +
				ndc[1] * ey +
				-image_plane_distance * ez
			)
		),
		raytype
	);
}