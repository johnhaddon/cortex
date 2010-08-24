//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2007-2010, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include <cassert>
#include <iostream>

#include "IECore/BlindDataHolder.h"

using namespace IECore;

const unsigned int BlindDataHolder::m_ioVersion = 1;

IE_CORE_DEFINEOBJECTTYPEDESCRIPTION(BlindDataHolder);

BlindDataHolder::BlindDataHolder()
{
	m_data = new CompoundData();
}

BlindDataHolder::BlindDataHolder(CompoundDataPtr data) : m_data(data)
{
	assert(m_data);
}

BlindDataHolder::BlindDataHolder( const BlindDataHolder &other )
{
	m_data = other.m_data;

	assert(m_data);
}

BlindDataHolder::~BlindDataHolder()
{
}

CompoundDataPtr BlindDataHolder::blindData() const
{
	return m_data;
}

void BlindDataHolder::copyFrom( const Object *other, CopyContext *context )
{
	Object::copyFrom( other, context );
	const BlindDataHolder *tOther = static_cast<const BlindDataHolder *>( other );
	m_data = context->copy<CompoundData>( tOther->m_data );
}

void BlindDataHolder::save( SaveContext *context ) const
{
	Object::save( context );
	IndexedIOInterfacePtr container = context->container( staticTypeName(), m_ioVersion );

	assert(m_data);
	context->save( m_data, container, "blindData");
}

void BlindDataHolder::load( LoadContextPtr context )
{
	Object::load( context );
	unsigned int v = m_ioVersion;
	IndexedIOInterfacePtr container = context->container( staticTypeName(), v );
	m_data = context->load<CompoundData>( container, "blindData");
	assert(m_data);
}

bool BlindDataHolder::isEqualTo( const Object *other ) const
{
	if( !Object::isEqualTo( other ) )
	{
		return false;
	}
	const BlindDataHolder *tOther = static_cast<const BlindDataHolder *>( other );

	return m_data->isEqualTo( tOther->m_data );
}

void BlindDataHolder::memoryUsage( Object::MemoryAccumulator &a ) const
{
	Object::memoryUsage( a );
	a.accumulate( m_data );
}