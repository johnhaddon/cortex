//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2013, Image Engine Design Inc. All rights reserved.
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

#ifndef IECOREMAYA_MAYASCENE_H
#define IECOREMAYA_MAYASCENE_H

#include "tbb/mutex.h"

#include "IECore/SceneInterface.h"
#include "IECoreMaya/TypeIds.h"

#include "maya/MDagPath.h"

namespace IECoreMaya
{

IE_CORE_FORWARDDECLARE( MayaScene );

/// A class for navigating a maya scene.
/// Each MayaScene instance maps to a specific transform in a scene, uniquely identified by it's dag path.
/// Shapes are interpreted as objects living on their parent - eg a scene with the objects |pSphere1 and
/// |pSphere1|pSphereShape1 in it will map to a MayaScene at "/", with a child called "pSphere1", with a
/// MeshPrimitive as its object, and no children.
/// This interface currently only supports read operations, which can only be called with the current maya
/// time in seconds. For example, if you're currently on frame 1 in your maya session, your scene's frame rate
/// is 24 fps, and you want to read an object from your MayaScene instance, you must call
/// mayaSceneInstance.readObject( 1.0 / 24 ), or it will throw an exception.

class MayaScene : public IECore::SceneInterface
{
	public :
		
		IE_CORE_DECLARERUNTIMETYPEDEXTENSION( MayaScene, MayaSceneTypeId, IECore::SceneInterface );
		
		// default constructor
		MayaScene();
		
		virtual ~MayaScene();
		
		virtual std::string fileName() const;

		/// Returns the name of the scene location which this instance is referring to. The root path returns "/".
		virtual Name name() const;
		/// Returns the tokenized dag path this instance is referring to.
		virtual void path( Path &p ) const;
		
		/*
		 * Bounding box
		 */

		/// Returns the local bounding box of this node at the
		/// specified point in time, which must be equal to the current maya time in seconds.
		virtual Imath::Box3d readBound( double time ) const;
		/// Not currently supported - will throw an exception.
		virtual void writeBound( const Imath::Box3d &bound, double time );

		/*
		 * Transform
		 */

		/// Returns the local transform of this node at the specified 
		/// point in time, which must be equal to the current maya time in seconds.
		virtual IECore::DataPtr readTransform( double time ) const;
		/// Returns the transform of this node at the specified 
		/// point in time as a matrix.
		virtual Imath::M44d readTransformAsMatrix( double time ) const;
		/// Not currently supported - will throw an exception.
		virtual void writeTransform( const IECore::Data *transform, double time );

		/*
		 * Attributes
		 */

		virtual bool hasAttribute( const Name &name ) const;
		/// Fills attrs with the names of all attributes available in the current directory
		virtual void readAttributeNames( NameList &attrs ) const;
		/// Returns the attribute value at the given time, which must be equal to the current maya time in seconds.
		virtual IECore::ObjectPtr readAttribute( const Name &name, double time ) const;
		/// Not currently supported - will throw an exception.
		virtual void writeAttribute( const Name &name, const IECore::Object *attribute, double time );

		/*
		 * Object
		 */

		/// Convenience method to determine if a piece of geometry exists without reading it
		virtual bool hasObject() const;
		/// Reads the object stored at this path in the scene at the given time - may
		/// return 0 when no object has been stored. Time must be equal to the current maya time in seconds
		virtual IECore::ObjectPtr readObject( double time ) const;
		/// Not currently supported - will throw an exception.
		virtual void writeObject( const IECore::Object *object, double time );

		/*
		 * Hierarchy
		 */

		/// Queries the names of any existing children of path() within
		/// the scene.
		virtual void childNames( NameList &childNames ) const;
		/// Queries weather the named child exists.
		virtual bool hasChild( const Name &name ) const;
		/// Returns an object for the specified child location in the scene.
		/// If the child does not exist then it will behave according to the 
		/// missingBehavior parameter. May throw and exception, may return a NULL pointer,
		/// or may create the child (if that is possible).
		/// Bounding boxes will be automatically propagated up from the children
		/// to the parent as it is written.
		virtual IECore::SceneInterfacePtr child( const Name &name, MissingBehaviour missingBehaviour = SceneInterface::ThrowIfMissing );
		/// Returns a read-only interface for a child location in the scene.
		virtual IECore::ConstSceneInterfacePtr child( const Name &name, MissingBehaviour missingBehaviour = SceneInterface::ThrowIfMissing ) const;
		/// Returns a writable interface to a new child. Throws an exception if it already exists.
		/// Bounding boxes will be automatically propagated up from the children
		/// to the parent as it is written.
		virtual IECore::SceneInterfacePtr createChild( const Name &name );
		
		
		/// Returns an object for querying the scene at the given path (full path).
		virtual IECore::SceneInterfacePtr scene( const Path &path, MissingBehaviour missingBehaviour = SceneInterface::ThrowIfMissing );
		
		/// Returns an object for querying the scene at the given path (full path). 
		virtual IECore::ConstSceneInterfacePtr scene( const Path &path, MissingBehaviour missingBehaviour = SceneInterface::ThrowIfMissing ) const;

	private :
				
		
		IECore::SceneInterfacePtr retrieveScene( const Path &path, MissingBehaviour missingBehaviour = SceneInterface::ThrowIfMissing ) const;
		IECore::SceneInterfacePtr retrieveChild( const Name &name, MissingBehaviour missingBehaviour = SceneInterface::ThrowIfMissing ) const;
		IECore::SceneInterfacePtr retrieveParent() const;
		
		void getChildDags( const MDagPath& dagPath, MDagPathArray& paths ) const;
		
	protected:
		
		// constructor for a specific dag path:
		MayaScene( const MDagPath& p, bool isRoot = false );
		
		MDagPath m_dagPath;
		bool m_isRoot;
		
		/// calls the constructor for a specific dag path. Derived classes can override this so their child() and scene() methods can 
		/// return instances of the derived class
		virtual MayaScenePtr duplicate( const MDagPath& p, bool isRoot = false ) const;
	
		typedef tbb::mutex Mutex;
		static Mutex s_mutex;
		
};

IE_CORE_DECLAREPTR( MayaScene )

} // namespace IECoreMaya

#endif // IECOREMAYA_MAYASCENE_H