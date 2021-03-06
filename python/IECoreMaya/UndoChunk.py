##########################################################################
#
#  Copyright (c) 2016, Image Engine Design Inc. All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are
#  met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#
#     * Neither the name of Image Engine Design nor the names of any
#       other contributors to this software may be used to endorse or
#       promote products derived from this software without specific prior
#       written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
#  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
#  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
##########################################################################

import functools

import maya.cmds

# TODO: This can be simplified with contextlib.contextmanager in python3
class UndoChunk( object ) :
	"""Context Manager / Decorator which performs all of its operations with an undo chuck

	This manager ensures that all operations in the block are performed within a new
	undo chunk and that the chunk is closed when the block exits.

	.. code-block:: python

		@UndoChunk( 'Chunk' )
		def doWork():
			pass

		with UndoChunk( 'Chunk' ):
			pass

	"""
	def __init__( self, chunkName ):
		super(UndoChunk, self).__init__()
		self._chunkName = chunkName

	def __enter__( self ) :
		self._prevState = maya.cmds.undoInfo( query=True, stateWithoutFlush=True )
		maya.cmds.undoInfo( stateWithoutFlush=True )
		maya.cmds.undoInfo( chunkName=self._chunkName, openChunk=True )

	def __exit__( self, type, value, traceBack ) :
		maya.cmds.undoInfo( chunkName=self._chunkName, closeChunk=True )
		maya.cmds.undoInfo( stateWithoutFlush=self._prevState )
		return False

	def __call__( self, callable ):
		@functools.wraps( callable )
		def managed_callable( *args, **kwargs ):
			with self:
				return callable( *args, **kwargs )
		return managed_callable
