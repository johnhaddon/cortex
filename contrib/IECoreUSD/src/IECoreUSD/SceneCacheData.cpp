//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2021, Image Engine Design Inc. All rights reserved.
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

#include "SceneCacheData.h"

#include "IECoreUSD/AttributeAlgo.h"
#include "IECoreUSD/DataAlgo.h"
#include "IECoreUSD/SceneCacheDataAlgo.h"
#include "IECoreUSD/PrimitiveAlgo.h"
#include "IECoreUSD/USDScene.h"

#include "IECoreScene/Camera.h"
#include "IECoreScene/CurvesPrimitive.h"
#include "IECoreScene/LinkedScene.h"
#include "IECoreScene/MeshPrimitive.h"
#include "IECoreScene/Primitive.h"
#include "IECoreScene/SampledSceneInterface.h"
#include "IECoreScene/SceneInterface.h"

#include "IECore/DataAlgo.h"
#include "IECore/SimpleTypedData.h"

#include "pxr/base/trace/trace.h"
#include "pxr/base/vt/dictionary.h"
#include "pxr/base/work/utils.h"

#include "pxr/pxr.h"

#include "pxr/usd/sdf/data.h"
#include "pxr/usd/sdf/listOp.h"
#include "pxr/usd/usd/clipsAPI.h"
#include "pxr/usd/usd/tokens.h"
#include "pxr/usd/usdGeom/tokens.h"

#include "boost/algorithm/string/erase.hpp"
#include "boost/algorithm/string/predicate.hpp"

#include <iostream>

using namespace IECore;
using namespace IECoreScene;
using namespace IECoreUSD;
using namespace Imath;
using namespace pxr;

namespace
{
static const TfToken g_vector( "Vector" );
const InternedString g_sampleTimes( "sampleTimes" );
static const TfToken g_xformTransform( "xformOp:transform" );
const InternedString g_cameraType( "ObjectType:Camera" );
static const TfToken g_camera( "Camera" );
const InternedString g_meshType( "ObjectType:MeshPrimitive" );
static const TfToken g_mesh( "Mesh" );
const InternedString g_pointsType( "ObjectType:PointsPrimitive" );
static const TfToken g_points( "Points" );
const InternedString g_curvesType( "ObjectType:CurvesPrimitive" );
static const TfToken g_curves( "BasisCurves" );
static const TfToken g_xform( "Xform" );
const InternedString g_pointPrimVar( "P" );
static const TfToken g_NormalsIndicesPrimVar( boost::str( boost::format( "%s:indices" ) % UsdGeomTokens->normals ) );
const InternedString g_normalPrimVar( "N" );
const InternedString g_uvPrimVar( "uv" );
const InternedString g_csPrimVar( "Cs" );
static const TfToken g_stPrimVar( "primvars:st" );
static const TfToken g_stIndicesPrimVar( "primvars:st:indices" );
const InternedString g_collectionPrimName( "cortex_tags" );
const InternedString g_widthPrimVar( "width" );
static const std::vector<std::string> g_defaultPrimVars = { UsdGeomTokens->orientation.GetString() };
static const std::vector<std::string> g_defaultAttributes = { SceneInterface::visibilityName, LinkedScene::fileNameLinkAttribute, LinkedScene::rootLinkAttribute, LinkedScene::timeLinkAttribute };
static const std::string g_sceneInterfacePrefix( "sceneInterface:" );
static const SceneInterface::Path g_staticIoVariablesPath = { "object", "0", "data", "Primitive", "data", "variables" };
static InternedString g_ioRoot( "root" );
static InternedString g_ioChildren( "children" );
static InternedString g_ioAttributes( "attributes" );
static InternedString g_firstTimeSample( "0" );
static InternedString g_ioInterpolation( "interpolation" );
const InternedString g_interpretation("interpretation");
static InternedString g_ioData( "data" );
static InternedString g_ioType( "type" );
static InternedString g_ioIndices( "indices" );
static const SceneInterface::Path g_mayaFpsHeaderPath = { "header", "data", "CompoundObject", "data", "members", "maya", "data", "CompoundDataBase", "data", "members", "frameRate", "data"};
static const SceneInterface::Path g_houdiniFpsHeaderPath = { "header", "data", "CompoundObject", "data", "members", "houdini", "data", "CompoundDataBase", "data", "members", "frameRate", "data"};
static const VtValue g_empty = VtValue();
static const InternedString g_expansionRule( "expansionRule" );
} // namespace


SceneCacheData::SceneCacheData( SdfFileFormat::FileFormatArguments args )
	: m_arguments(std::move(args))
{
}

SceneCacheDataRefPtr SceneCacheData::New( SdfFileFormat::FileFormatArguments args )
{
	return TfCreateRefPtr( new SceneCacheData( std::move( args ) ) );
}
void SceneCacheData::addValueClip( SpecData& spec, const VtVec2dArray times, const VtVec2dArray actives, const std::string& assetPath, const std::string& primPath )
{
	if ( !times.empty() )
	{
		// asset paths
		VtArray<SdfAssetPath> assetPaths;
		assetPaths.push_back( SdfAssetPath( assetPath ) );

		spec.fields.push_back(
			FieldValuePair(
				UsdTokens->clips,
				VtDictionary(
					{
						{
							UsdClipsAPISetNames->default_, VtValue(
								VtDictionary(
								{
									{ UsdClipsAPIInfoKeys->primPath, VtValue( primPath ) },
									{ UsdClipsAPIInfoKeys->assetPaths, VtValue( assetPaths ) },
									{ UsdClipsAPIInfoKeys->times, VtValue( times ) },
									{ UsdClipsAPIInfoKeys->active, VtValue( actives ) }
								}
								)
							)
						},
					}
				)
			)
		);
	}

}

void SceneCacheData::addReference( ConstSceneInterfacePtr scene, SpecData& spec, TfTokenVector& children )
{
	// USD doesn't support animated reference asset path so we need to read the link at an arbitrary time.
	auto linkFileNameData = runTimeCast<const StringData>( scene->readAttribute( LinkedScene::fileNameLinkAttribute, 0 ) );
	auto linkRootData = runTimeCast<const InternedStringVectorData>( scene->readAttribute( LinkedScene::rootLinkAttribute, 0 ) );

	if( !linkFileNameData || !linkRootData )
	{
		return;
	}
	auto& linkFileName = linkFileNameData->readable();
	auto& linkRoot = linkRootData->readable();

	VtVec2dArray times;
	VtVec2dArray actives;

	if ( const SampledSceneInterface * sampledScene = runTimeCast<const SampledSceneInterface>( scene.get() ) )
	{
		auto timeLinkSamples = 0;
		try
		{
			timeLinkSamples = sampledScene->numAttributeSamples( LinkedScene::timeLinkAttribute );
		}
		catch( ... )
		{
		}

		if ( timeLinkSamples )
		{
			// active first clip in the asset path index as we always have only one clip
			actives.push_back( GfVec2d( timeToFrame( sampledScene->attributeSampleTime( LinkedScene::timeLinkAttribute, 0 ) ), 0 ) );

			// time
			for ( signed int i=0; i < timeLinkSamples; i++ )
			{
				const auto sampleTime = sampledScene->attributeSampleTime( LinkedScene::timeLinkAttribute, i );
				const auto timeValue = runTimeCast<const DoubleData>( sampledScene->readAttribute( LinkedScene::timeLinkAttribute, sampleTime ) );
				times.push_back( GfVec2d( timeToFrame( sampleTime ), timeToFrame( timeValue->readable() ) ) );
			}
		}
	}

	SdfPath linkRootPath = SdfPath::AbsoluteRootPath().AppendChild( SceneCacheDataAlgo::internalRootNameToken() );
	for ( auto& linkPath : linkRoot )
	{
		linkRootPath = linkRootPath.AppendChild( TfToken( linkPath.value() ) );
	}

	SdfReferenceListOp refListOp;
	SdfReference reference( linkFileName, linkRootPath );
	refListOp.SetPrependedItems( { reference } );

	spec.fields.push_back( FieldValuePair( SdfFieldKeys->References, refListOp ) );

	// value clip for time remapping
	addValueClip( spec, times, actives, linkFileName, linkRootPath.GetText() );
}

void SceneCacheData::addInternalRoot( TfTokenVector children )
{
	// add transform for internal root.
	SdfPath internalRootPath = SdfPath::AbsoluteRootPath().AppendChild( SceneCacheDataAlgo::internalRootNameToken() );

	// define prim for internal root child
	SpecData internalRootSpec;

	// spec type
	internalRootSpec.specType = SdfSpecTypePrim;

	// specifier: how the PrimSpec should be consumed and interpreted in a composed scene
	internalRootSpec.fields.push_back( FieldValuePair( SdfFieldKeys->Specifier, SdfSpecifierDef ) );

	// typename
	internalRootSpec.fields.push_back( FieldValuePair( SdfFieldKeys->TypeName, g_xform ) );

	// steal root children
	internalRootSpec.fields.push_back( FieldValuePair( SdfChildrenKeys->PrimChildren, children ) );

	m_data[internalRootPath] = internalRootSpec;
}

void SceneCacheData::loadSceneIntoCache( ConstSceneInterfacePtr scene )
{
	SceneInterface::Path scenePath;
	scene->path( scenePath );

	// insert internal root into path
	auto currentPath = SceneCacheDataAlgo::toInternalPath( scenePath );

	// store translation to internal path
	m_internalPaths[currentPath] = scenePath;

	SdfPath primPath = USDScene::toUSD(currentPath);

	// reset the collection map for each internal root child
	if( primPath.GetPathElementCount() == 2 )
	{
		m_collections.clear();
	}

	TfTokenVector children;
	SpecData spec;

	// load link as reference
	if( scene->hasAttribute( LinkedScene::fileNameLinkAttribute ) )
	{
		addReference( scene, spec, children );
	}
	else
	{
		// children
		SceneInterface::NameList childNames;
		scene->childNames( childNames );

		for ( auto& child: childNames )
		{
			children.push_back( TfToken( SceneCacheDataAlgo::toInternalName( child ) ) );

			// recurse
			ConstSceneInterfacePtr childScene = scene->child( child );
			loadSceneIntoCache( childScene );
		}
	}

	if ( currentPath == SceneInterface::rootPath )
	{
		spec.specType = SdfSpecTypePseudoRoot;

		// default prim
		spec.fields.push_back( FieldValuePair( SdfFieldKeys->DefaultPrim, SceneCacheDataAlgo::internalRootNameToken() ) );

		// frame per second
		spec.fields.push_back( FieldValuePair( SdfFieldKeys->TimeCodesPerSecond, m_fps ) );

		// figure out start and end frame based on timeSamples in header
		float minTime = Imath::limits<float>::max();
		float maxTime = 0;
		bool validTimeSampleRange = false;
		if ( auto sampleTimesDir = m_sceneio->subdirectory( g_sampleTimes, IndexedIO::MissingBehaviour::NullIfMissing ) )
		{
			IndexedIO::EntryIDList sampleLists;
			sampleTimesDir->entryIds( sampleLists );
			for ( auto& sampleList : sampleLists )
			{
				auto entry = sampleTimesDir->entry( sampleList );
				auto count = entry.arrayLength();

				std::vector<double> times;
				times.resize( count );
				auto data = &times[0];

				sampleTimesDir->read( sampleList, data, count );
				// skipping single sample at 0 sec.
				if ( count == 1 && times[0] == 0 )
				{
					continue;
				}
				for ( auto& time : times )
				{
					if( time < minTime )
					{
						minTime = time;
						validTimeSampleRange = true;
					}
					if( time > maxTime )
					{
						maxTime = time;
					}
				}
			}
		}

		double startFrame = 0.0;
		double lastFrame = 0.0;
		if ( validTimeSampleRange )
		{
			startFrame = std::round( timeToFrame( minTime ) );
			lastFrame = std::round( timeToFrame( maxTime ) );
		}
		// start timecode
		spec.fields.push_back( FieldValuePair( SdfFieldKeys->StartTimeCode, startFrame ) );

		// end timecode
		spec.fields.push_back( FieldValuePair( SdfFieldKeys->EndTimeCode, lastFrame ) );

		addInternalRoot( children );

		// add internal root as single child
		children.clear();
		children.push_back( SceneCacheDataAlgo::internalRootNameToken() );
	}
	else
	{
		// specifier: how the PrimSpec should be consumed and interpreted in a composed scene
		spec.fields.push_back( FieldValuePair( SdfFieldKeys->Specifier, SdfSpecifierDef ) );

		spec.specType = SdfSpecTypePrim;

		// children properties
		FieldValuePair propertyChildren;
		propertyChildren.first = SdfChildrenKeys->PropertyChildren;
		TfTokenVector properties;

		// prim type name based on tag
		TfToken typeName;

		// custom attributes
		loadAttributes( currentPath, properties, typeName );

		// visibility
		addProperty( properties, primPath, UsdGeomTokens->visibility, SdfValueTypeNames->Token, false, SdfVariabilityVarying );

		// extent
		addProperty( properties, primPath, UsdGeomTokens->extent, SdfValueTypeNames->Float3Array, false, SdfVariabilityVarying );

		// xformOpOrder
		addProperty( properties, primPath, UsdGeomTokens->xformOpOrder, SdfValueTypeNames->TokenArray, false, SdfVariabilityUniform, &g_xformTransform );

		// xformOp:transform
		addProperty( properties, primPath, g_xformTransform, SdfValueTypeNames->Matrix4d, false, SdfVariabilityVarying );

		// build map for collections
		SceneInterface::NameList tags;
		scene->readTags( tags );
		for ( auto& tag : tags )
		{
			m_collections[tag].push_back( primPath );
		}

		if ( scene->hasObject() )
		{
			if( scene->hasTag( g_cameraType ) )
			{
				typeName = g_camera;

				// focal length
				addProperty( properties, primPath, UsdGeomTokens->focalLength, SdfValueTypeNames->Float, false, SdfVariabilityVarying );

				// horizontal aperture
				addProperty(
					properties,
					primPath,
					UsdGeomTokens->horizontalAperture,
					SdfValueTypeNames->Float,
					false,
					SdfVariabilityVarying
				);
				// vertical aperture
				addProperty( properties, primPath, UsdGeomTokens->verticalAperture, SdfValueTypeNames->Float, false, SdfVariabilityVarying );

				// horizontal aperture offset
				addProperty(
					properties,
					primPath,
					UsdGeomTokens->horizontalApertureOffset,
					SdfValueTypeNames->Float,
					false,
					SdfVariabilityVarying
				);

				// vertical aperture offset
				addProperty(
					properties,
					primPath,
					UsdGeomTokens->verticalApertureOffset,
					SdfValueTypeNames->Float,
					false,
					SdfVariabilityVarying
				);
			}
			else
			{
				if( scene->hasTag( g_meshType ) )
				{
					typeName = g_mesh;

					// topology

					// verticesPerFace
					addProperty(
						properties,
						primPath,
						UsdGeomTokens->faceVertexCounts,
						SdfValueTypeNames->IntArray,
						false,
						SdfVariabilityVarying
					);

					// vertexIds
					addProperty( properties, primPath, UsdGeomTokens->faceVertexIndices, SdfValueTypeNames->IntArray, false, SdfVariabilityVarying );

					// cornerIndices
					addProperty( properties, primPath, UsdGeomTokens->cornerIndices, SdfValueTypeNames->IntArray, false, SdfVariabilityVarying );

					// cornerSharpness
					addProperty(
						properties,
						primPath,
						UsdGeomTokens->cornerSharpnesses,
						SdfValueTypeNames->FloatArray,
						false,
						SdfVariabilityVarying
					);

					// creaseIndices
					addProperty( properties, primPath, UsdGeomTokens->creaseIndices, SdfValueTypeNames->IntArray, false, SdfVariabilityVarying );

					// creaseLengths
					addProperty( properties, primPath, UsdGeomTokens->creaseLengths, SdfValueTypeNames->IntArray, false, SdfVariabilityVarying );

					// creaseSharpness
					addProperty(
						properties,
						primPath,
						UsdGeomTokens->creaseSharpnesses,
						SdfValueTypeNames->FloatArray,
						false,
						SdfVariabilityVarying
					);
				}
				else if( scene->hasTag( g_pointsType ) )
				{
					typeName = g_points;
				}
				else if( scene->hasTag( g_curvesType ) )
				{
					typeName = g_curves;

					// curve type
					addProperty( properties, primPath, UsdGeomTokens->type, SdfValueTypeNames->Token, false, SdfVariabilityVarying, &UsdGeomTokens->linear, false );

					// curve basis
					addProperty( properties, primPath, UsdGeomTokens->basis, SdfValueTypeNames->Token, false, SdfVariabilityVarying );

					// curve wrap
					addProperty(
						properties,
						primPath,
						UsdGeomTokens->wrap,
						SdfValueTypeNames->Token,
						false,
						SdfVariabilityUniform,
						&UsdGeomTokens->nonperiodic,
						false
					);

					// verticesPerCurve
					addProperty(
						properties,
						primPath,
						UsdGeomTokens->curveVertexCounts,
						SdfValueTypeNames->IntArray,
						false,
						SdfVariabilityVarying
					);

				}
				// prim vars
				loadPrimVars( currentPath, properties, typeName );

				// orientation
				addProperty(
					properties,
					primPath,
					UsdGeomTokens->orientation,
					SdfValueTypeNames->Token,
					false,
					SdfVariabilityUniform,
					&UsdGeomTokens->rightHanded,
					false,
					&UsdGeomTokens->vertex,
					false
				);
			}
		}
		else
		{
			typeName = g_xform;
		}
		if ( primPath.GetPathElementCount() == 2 )
		{
			addCollections( spec, properties, primPath );
		}

		propertyChildren.second = properties;
		spec.fields.push_back( propertyChildren );

		spec.fields.push_back( FieldValuePair( SdfFieldKeys->TypeName, typeName ) );
	}
	// common to both pseudo root and prim spec
	// children prims
	spec.fields.push_back( FieldValuePair( SdfChildrenKeys->PrimChildren, children ) );

	m_data[primPath] = spec;
}

void SceneCacheData::loadAttributes( const SceneInterface::Path& currentPath, TfTokenVector& properties, TfToken& PrimTypeName )
{
	SdfPath primPath = USDScene::toUSD(currentPath);

	// variables
	SceneInterface::Path attributesPath;
	attributesPath.push_back( g_ioRoot );
	for ( auto& p : currentPath )
	{
		// avoid injecting the internal root because
		// the path would be invalid in the IndexedIO hierarchy
		if ( p == SceneCacheDataAlgo::internalRootName() )
		{
			continue;
		}
		attributesPath.push_back( g_ioChildren );
		attributesPath.push_back( SceneCacheDataAlgo::fromInternalName( p ) );
	}

	attributesPath.push_back( g_ioAttributes );

	if ( auto attributes = m_sceneio->directory( attributesPath, IndexedIO::MissingBehaviour::NullIfMissing ) )
	{
		IndexedIO::EntryIDList attributeLists;
		attributes->entryIds( attributeLists );
		for( auto& attr: attributeLists )
		{

			// ignore all private sceneInterface:*
			if ( boost::starts_with( attr.string(), g_sceneInterfacePrefix ) )
			{
				continue;
			}

			auto it = find( g_defaultAttributes.cbegin(), g_defaultAttributes.cend(), attr.value() );
			if( it != g_defaultAttributes.cend() )
			{
				continue;
			}

			auto attributeIO = attributes->subdirectory( attr, IndexedIO::MissingBehaviour::NullIfMissing );

			auto timeSamplesIO = attributeIO->subdirectory( g_firstTimeSample, IndexedIO::MissingBehaviour::NullIfMissing );
			if ( !timeSamplesIO )
			{
				IECore::msg( IECore::Msg::Warning, "SceneCacheData::loadAttributes", boost::format( "Unable to find time samples for attribute \"%s\" at location \"%s\"." ) % attr % primPath );
				continue;
			}

			// data type
			std::string dataTypeValue;
			if( !timeSamplesIO->hasEntry( g_ioType ) )
			{
				IECore::msg( IECore::Msg::Warning, "SceneCacheData::loadAttributes", boost::format( "Unable to find data type directory for attribute \"%s\" at location \"%s\"." ) % attr % primPath );
				continue;
			}
			timeSamplesIO->read( g_ioType, dataTypeValue );

			// find the usd type corresponding to our cortex one
			SdfValueTypeName usdType;

			auto object = Object::create( dataTypeValue );
			if( auto data = runTimeCast<Data>( object.get() ) )
			{
				if ( timeSamplesIO->hasEntry( g_interpretation ) )
				{
					int interpretationValue;
					timeSamplesIO->read( g_interpretation, interpretationValue );
					try
					{
						setGeometricInterpretation( data, static_cast<GeometricData::Interpretation>( interpretationValue ) );
					}
					catch( ... )
					{
					}
				}
				usdType = DataAlgo::valueTypeName( data );
			}
			else
			{
				IECore::msg( IECore::Msg::Warning, "SceneCacheData::loadAttributes", boost::format( "Unable to convert to USD data type for attribute \"%s\" at location \"%s\"." ) % attr % primPath );
				continue;
			}
			
			addProperty(
				properties,
				primPath,
				TfToken( attr ),
				usdType,
				/*custom=*/true,
				/*variability=*/SdfVariabilityVarying,
				/*defaultValue=*/nullptr,
				/*defaultValueIsArray=*/false,
				/*interpolation=*/nullptr,
				/*useObjectSample=*/false,
				/*isCortexAttribute=*/true
			);
		}
	}
}
void SceneCacheData::loadPrimVars( const SceneInterface::Path& currentPath, TfTokenVector& properties, TfToken& PrimTypeName )
{
	SdfPath primPath = USDScene::toUSD(currentPath);

	// variables
	SceneInterface::Path variablesPath;
	variablesPath.push_back( g_ioRoot );
	for ( auto& p : currentPath )
	{
		// avoid injecting the internal root because
		// the path would be invalid in the IndexedIO hierarchy
		if ( p == SceneCacheDataAlgo::internalRootName() )
		{
			continue;
		}
		variablesPath.push_back( g_ioChildren );
		variablesPath.push_back( SceneCacheDataAlgo::fromInternalName( p ) );
	}

	variablesPath.insert( variablesPath.end(), g_staticIoVariablesPath.begin(), g_staticIoVariablesPath.end() );

	if ( auto variables = m_sceneio->directory( variablesPath, IndexedIO::MissingBehaviour::NullIfMissing ) )
	{
		IndexedIO::EntryIDList variableLists;
		variables->entryIds( variableLists );
		for( auto& var: variableLists )
		{
			auto it = find( g_defaultPrimVars.cbegin(), g_defaultPrimVars.cend(), var.value() );
			if( it != g_defaultPrimVars.cend() )
			{
				continue;
			}

			// interpolation
			auto variableIO = variables->subdirectory( var, IndexedIO::MissingBehaviour::NullIfMissing );
			int interpolationValue = 0;
			TfToken usdInterpolation;
			if ( !variableIO || !variableIO->hasEntry( g_ioInterpolation ) )
			{
				IECore::msg( IECore::Msg::Warning, "SceneCacheData::loadPrimVars", boost::format( "Unable to find interpolation for Primitive Variable \"%s\" at location \"%s\"." ) % var % primPath );
				continue;
			}

			variableIO->read( g_ioInterpolation, interpolationValue );
			usdInterpolation = PrimitiveAlgo::toUSD( static_cast<PrimitiveVariable::Interpolation>( interpolationValue ) );

			// data type
			auto dataType = variableIO->subdirectory( g_ioData, IndexedIO::MissingBehaviour::NullIfMissing );
			std::string dataTypeValue;
			if( !dataType || !dataType->hasEntry( g_ioType ) )
			{
				IECore::msg( IECore::Msg::Warning, "SceneCacheData::loadPrimVars", boost::format( "Unable to find data type for Primitive Variable \"%s\" at location \"%s\"." ) % var % primPath );
				continue;
			}
			dataType->read( g_ioType, dataTypeValue );

			// interpretation
			auto interpretationData = dataType->subdirectory( g_ioData, IndexedIO::MissingBehaviour::NullIfMissing );
			IntDataPtr interpretationValue = nullptr;
			if ( interpretationData && interpretationData->hasEntry( g_interpretation ) )
			{
				interpretationValue = new IntData;
				interpretationData->read( g_interpretation, interpretationValue->writable() );
			}

			// find the usd type corresponding to our cortex one
			SdfValueTypeName usdType;
			TfToken primVarName;
			if ( var == g_pointPrimVar )
			{
				primVarName = UsdGeomTokens->points;
				usdType = SdfValueTypeNames->Point3fArray;
			}
			else if ( var == g_normalPrimVar )
			{
				primVarName = UsdGeomTokens->normals;
				usdType = SdfValueTypeNames->Normal3fArray;
			}
			else if ( var == g_widthPrimVar )
			{
				primVarName = UsdGeomTokens->widths;
			}
			else if ( var == UsdGeomTokens->accelerations.GetText() && PrimTypeName == g_points )
			{
				usdType = SdfValueTypeNames->Vector3fArray;
			}
			else if ( var == UsdGeomTokens->velocities.GetText() && PrimTypeName == g_points )
			{
				usdType = SdfValueTypeNames->Vector3fArray;
			}
			else if ( var == g_uvPrimVar )
			{
				primVarName = g_stPrimVar;
				usdType = SdfValueTypeNames->TexCoord2fArray;
			}
			else if ( var == g_csPrimVar )
			{
				primVarName = UsdGeomTokens->primvarsDisplayColor;
			}
			else
			{
				primVarName = TfToken( boost::str( boost::format( "primvars:%s" ) % var ) );
				auto object = Object::create( dataTypeValue );
				if( auto data = runTimeCast<Data>( object.get() ) )
				{
					if ( interpretationValue != nullptr )
					{
						try
						{
							setGeometricInterpretation( data, static_cast<GeometricData::Interpretation>( interpretationValue->readable() ) );
						}
						catch( ... )
						{
						}
					}
					usdType = DataAlgo::valueTypeName( data );
				}
				else
				{
					IECore::msg( IECore::Msg::Warning, "SceneCacheData::loadPrimVars", boost::format( "Unable to find USD data type for Primitive Variable \"%s\" at location \"%s\"." ) % var % primPath );
					continue;
				}
			}

			addProperty(
				properties,
				primPath,
				primVarName,
				usdType,
				false, // As far as I can tell from USD documentation and examples, "custom" means "not matching
				       // any schema".  There is a schema for primvars, therefore no primvars are custom
				SdfVariabilityVarying,
				/*default value=*/nullptr,
				/* default value is array=*/false,
				&usdInterpolation,
				/* use object sample=*/true
			);

			// indices
			if( variableIO && variableIO->hasEntry( g_ioIndices ) )
			{
				TfToken primVarIndicesName = TfToken( boost::str( boost::format( "%s:indices" ) % primVarName ) );

				addProperty(
					properties,
					primPath,
					primVarIndicesName,
					SdfValueTypeNames->IntArray,
					false, // Indices are definitely part of the primvar schema, not a custom attribute
					SdfVariabilityVarying,
					/*default value=*/nullptr,
					/*default value is array=*/false,
					&usdInterpolation,
					/*use object sample=*/true
				);
			}
		}
	}
}

void SceneCacheData::loadFps()
{
	// fallback fps
	// fps is stored as float in the header when coming from a dcc.
	float fps = 24.0f;
	// if the cache comes from a dcc it should have the frame per second in the header
	// todo\ we should make the header path for the frame per second (fps) more generic and also add support for Gaffer.
	for ( auto& headerPath : { g_mayaFpsHeaderPath, g_houdiniFpsHeaderPath } )
	{
		auto header = m_sceneio->directory( headerPath, IndexedIO::MissingBehaviour::NullIfMissing );
		if ( header )
		{
			header->read( "value", fps );
			break;
		}
	}
	m_fps = double( fps );
}

double SceneCacheData::timeToFrame( double time ) const
{
	// Round the result so we get exact frames in the common
	// case of times stored in seconds and frame rate = 1/24.
	static const double P = 1.0e+10;
	return GfRound(P * ( time * m_fps )) / P;
}

double SceneCacheData::frameToTime( double frame ) const
{
	// Round the result so we get exact frames in the common
	// case of times stored in seconds and frame rate = 1/24.
	static const double P = 1.0e+10;
	return GfRound(P * ( frame / m_fps )) / P;
}

void SceneCacheData::addIncludeRelationship(
	const SdfPath& primPath,
	const TfToken& relationshipName,
	const SdfVariability & variability,
	const SdfListOp<SdfPath> & targetPaths,
	const std::vector<SdfPath> & targetChildren
	)
{

	// build path to relationship
	SdfPath relationshipPath = primPath.AppendProperty( relationshipName );

	SpecData spec;
	spec.specType = SdfSpecTypeRelationship;

	// variability
	spec.fields.push_back( FieldValuePair( SdfFieldKeys->Variability, variability ) );

	// target paths
	spec.fields.push_back( FieldValuePair( SdfFieldKeys->TargetPaths, targetPaths ) );

	// targetChildren
	spec.fields.push_back( FieldValuePair( SdfChildrenKeys->RelationshipTargetChildren, targetChildren ) );

	m_data[relationshipPath] = spec;
}

void SceneCacheData::addProperty(
	TfTokenVector& properties,
	const SdfPath& primPath,
	const TfToken& attributeName,
	const SdfValueTypeName& typeName,
	bool custom,
	const SdfVariability & variability,
	const TfToken * defaultValue,
	bool defaultValueIsArray,
	const TfToken * interpolation,
	bool useObjectSample,
	bool isCortexAttribute
	)
{
	// build path to attribute
	SdfPath attributePath;
	if ( isCortexAttribute )
	{
		std::string attrStr = attributeName.GetString();
		AttributeAlgo::Name usdName = AttributeAlgo::nameToUSD( attrStr );
		if( usdName.isPrimvar )
		{
			usdName.name = TfToken( "primvars:" + usdName.name.GetString() );
		}
		properties.push_back( usdName.name );
		attributePath = primPath.AppendProperty( usdName.name );
	}
	else
	{
		properties.push_back( attributeName );
		attributePath = primPath.AppendProperty( attributeName );
	}

	SpecData spec;
	spec.specType = SdfSpecTypeAttribute;

	// variability
	spec.fields.push_back( FieldValuePair( SdfFieldKeys->Variability, variability ) );

	if( isCortexAttribute )
	{
		spec.fields.push_back( FieldValuePair( UsdGeomTokens->interpolation, UsdGeomTokens->constant ) );
	}

	// default value
	if( defaultValue )
	{
		FieldValuePair defaultValueField;
		defaultValueField.first = SdfFieldKeys->Default;
		if ( defaultValueIsArray )
		{
			VtArray<TfToken> defaultValueArray(1);
			*defaultValueArray.data() = *defaultValue;
			defaultValueField.second = defaultValueArray;
		}
		else
		{
			defaultValueField.second = *defaultValue;
		}
		spec.fields.push_back( defaultValueField );
	}

	// interpolation
	if( interpolation )
	{
		spec.fields.push_back( FieldValuePair( UsdGeomTokens->interpolation, *interpolation ) );
		if ( !isCortexAttribute && *interpolation == UsdGeomTokens->constant )
		{
			spec.fields.push_back( FieldValuePair( AttributeAlgo::cortexPrimitiveVariableMetadataToken(), true ) );
		}
	}

	// time samples
	if ( variability == SdfVariabilityVarying )
	{
		FieldValuePair timeSamplesField;
		timeSamplesField.first = SdfFieldKeys->TimeSamples;

		// fallback
		SdfTimeSampleMap sampleMap;

		auto path = m_internalPaths.at( USDScene::fromUSD( primPath ) );
		auto currentScene = m_scene->scene( path );
		const SampledSceneInterface * sampledScene = dynamic_cast<const SampledSceneInterface *>( currentScene.get() );

		if ( attributeName == g_xformTransform )
		{
			if ( sampledScene )
			{
				for ( size_t i=0; i < sampledScene->numTransformSamples(); i++ )
				{
					double time = timeToFrame( sampledScene->transformSampleTime( i ) );
					sampleMap[time]; // we are not loading the data here to delay load it in queryTimeSample instead
				}
			}
		}
		else if ( attributeName == UsdGeomTokens->visibility )
		{
			if ( sampledScene )
			{
				size_t visibilitySamples = 0;
				try
				{
					 visibilitySamples = sampledScene->numAttributeSamples(SceneInterface::visibilityName);
				}
				catch( ... )
				{
				}

				if ( visibilitySamples )
				{
					for ( size_t i=0; i < visibilitySamples; i++ )
					{
						double time = timeToFrame( sampledScene->attributeSampleTime(SceneInterface::visibilityName, i ) );
						sampleMap[time]; // we are not loading the data here to delay load it in queryTimeSample instead
					}
				}
				else
				{
					// add a sample at time 0 for static attribute
					sampleMap[0];
				}
			}
		}
		else if ( attributeName == UsdGeomTokens->extent )
		{
			if ( sampledScene )
			{
				for ( size_t i=0; i < sampledScene->numBoundSamples(); i++ )
				{
					double time = timeToFrame( sampledScene->boundSampleTime( i ) );
					sampleMap[time]; // we are not loading the data here to delay load it in queryTimeSample instead
				}
			}
		}
		else if (
			attributeName == UsdGeomTokens->faceVertexCounts ||
			attributeName == UsdGeomTokens->faceVertexIndices ||
			attributeName == UsdGeomTokens->cornerIndices ||
			attributeName == UsdGeomTokens->cornerSharpnesses ||
			attributeName == UsdGeomTokens->creaseIndices ||
			attributeName == UsdGeomTokens->creaseLengths ||
			attributeName == UsdGeomTokens->creaseSharpnesses ||
			attributeName == UsdGeomTokens->curveVertexCounts ||
			attributeName == UsdGeomTokens->focalLength ||
			attributeName == UsdGeomTokens->horizontalAperture ||
			attributeName == UsdGeomTokens->verticalAperture ||
			attributeName == UsdGeomTokens->horizontalApertureOffset ||
			attributeName == UsdGeomTokens->verticalApertureOffset ||
			attributeName == UsdGeomTokens->basis ||
			attributeName == UsdGeomTokens->type ||
			useObjectSample
			)
		{
			if ( sampledScene )
			{
				size_t objectSamples = 0;
				try
				{
					 objectSamples = sampledScene->numObjectSamples();
				}
				catch( ... )
				{
				}

				if ( objectSamples )
				{
					for ( size_t i=0; i < objectSamples; i++ )
					{
						double time = timeToFrame( sampledScene->objectSampleTime( i ) );
						sampleMap[time]; // we are not loading the data here to delay load it in queryTimeSample instead
					}
				}
				else
				{
					// add a sample at time 0 for static mesh
					sampleMap[0];
				}
			}
		}
		// custom attribute
		else if ( sampledScene->hasAttribute( attributeName.GetString() ) )
		{
			std::string attribute = attributeName.GetString();
			size_t attributeSamples = 0;
			try
			{
				 attributeSamples = sampledScene->numAttributeSamples(attribute);
			}
			catch( ... )
			{
			}

			if ( attributeSamples )
			{
				for ( size_t i=0; i < attributeSamples; i++ )
				{
					double time = timeToFrame( sampledScene->attributeSampleTime(attribute, i ) );
					sampleMap[time]; // we are not loading the data here to delay load it in queryTimeSample instead
				}
			}
			else
			{
				// add a sample at time 0 for static attribute
				sampleMap[0];
			}
		}

		timeSamplesField.second = sampleMap;
		spec.fields.push_back( timeSamplesField );
	}

	// custom
	spec.fields.push_back( FieldValuePair( SdfFieldKeys->Custom, custom ) );

	// typename
	spec.fields.push_back( FieldValuePair( SdfFieldKeys->TypeName, typeName.GetAsToken() ) );

	m_data[attributePath] = spec;
}

void SceneCacheData::addCollections( SpecData& spec, TfTokenVector& properties, const SdfPath& primPath )
{
	// apiSchemas
	FieldValuePair apiSchemas;
	apiSchemas.first = UsdTokens->apiSchemas;
	TfTokenVector collectionList;

	for ( auto& collection : m_collections )
	{
		auto safeCollectionName = SceneCacheDataAlgo::toInternalName( collection.first );

		// apiSchemas
		collectionList.push_back( TfToken( boost::str( boost::format( "CollectionAPI:%s" ) % safeCollectionName ) ) );

		// expansion rule
		TfToken expansionRuleName( boost::str( boost::format( "collection:%s:%s" ) % safeCollectionName % g_expansionRule ) );
		addProperty(
			properties,
			primPath,
			expansionRuleName,
			SdfValueTypeNames->Token,
			false,
			SdfVariabilityUniform,
			&UsdTokens->explicitOnly,
			false
		);
		
		// include relationship
		TfToken relationshipName( boost::str( boost::format( "collection:%s:includes" ) % safeCollectionName ) );
		SdfListOp<SdfPath> targetPaths;
		std::vector<SdfPath> targetChildren;

		SdfPathVector includePaths;
		for( auto& path : collection.second )
		{
			// include relationship target
			SdfPath includePath( path );
			includePaths.push_back( includePath );
			targetChildren.push_back( includePath );
		}
		targetPaths.SetExplicitItems( includePaths );

		addIncludeRelationship( primPath, relationshipName, SdfVariabilityUniform, targetPaths, targetChildren );
		properties.push_back( relationshipName );
	}

	// apiSchemas
	SdfListOp<TfToken> listOp;
	listOp.SetPrependedItems( collectionList );
	apiSchemas.second = listOp;

	spec.fields.push_back( apiSchemas );

}

bool SceneCacheData::Open( const std::string& filePath )
{
	m_scene = SharedSceneInterfaces::get( filePath );
	m_sceneio = IndexedIO::create( filePath, IndexedIO::rootPath, IndexedIO::Read );

	loadFps();
	loadSceneIntoCache( m_scene );

	return true;
}

SceneCacheData::~SceneCacheData()
{
	// Clear out m_data in parallel, since it can get big.
	WorkSwapDestroyAsync(m_data);
}

bool SceneCacheData::StreamsData() const
{
	return true;
}

bool SceneCacheData::HasSpec(const SdfPath &path) const
{
	return m_data.find(path) != m_data.end();
}

void SceneCacheData::EraseSpec(const SdfPath &path)
{
	HashTable::iterator i = m_data.find(path);
	if (!TF_VERIFY(i != m_data.end(), "No spec to erase at <%s>", path.GetText()))
	{
		return;
	}
	m_data.erase(i);
}

void SceneCacheData::MoveSpec(const SdfPath &oldPath, const SdfPath &newPath)
{
	HashTable::iterator old = m_data.find(oldPath);
	if (!TF_VERIFY(old != m_data.end(), "No spec to move at <%s>", oldPath.GetString().c_str()))
	{
		return;
	}
	bool inserted = m_data.insert(std::make_pair(newPath, old->second)).second;
	if (!TF_VERIFY(inserted))
	{
		return;
	}
	m_data.erase(old);
}

SdfSpecType SceneCacheData::GetSpecType(const SdfPath &path) const
{
	if ( path.IsTargetPath() )
	{
		return SdfSpecTypeRelationshipTarget;
	}
	HashTable::const_iterator i = m_data.find(path);
	if (i == m_data.end())
	{
		return SdfSpecTypeUnknown;
	}
	return i->second.specType;
}

void SceneCacheData::CreateSpec(const SdfPath &path, SdfSpecType specType)
{
	if (!TF_VERIFY(specType != SdfSpecTypeUnknown))
	{
		return;
	}
	m_data[path].specType = specType;
}

void SceneCacheData::_VisitSpecs(SdfAbstractDataSpecVisitor* visitor) const
{
	TF_FOR_ALL(it, m_data)
	{
		if (!visitor->VisitSpec(*this, it->first))
		{
			break;
		}
	}
}

bool SceneCacheData::Has(const SdfPath &path, const TfToken &field, SdfAbstractDataValue* value) const
{
	const VtValue fieldValue = GetFieldValue(path, field);
	if ( fieldValue != g_empty)
	{
		if (value)
		{
			return value->StoreValue(fieldValue);
		}
		return true;
	}
	return false;
}

bool SceneCacheData::Has(const SdfPath &path, const TfToken & field, VtValue *value) const
{
	const VtValue fieldValue = GetFieldValue(path, field);
	if ( fieldValue != g_empty )
	{
		if (value)
		{
			*value = fieldValue;
		}
		return true;
	}
	return false;
}

VtValue SceneCacheData::getTimeSampleMap( const SdfPath& path, const TfToken& field, const VtValue& value ) const
{
	const SdfTimeSampleMap & timeSampleMap = value.UncheckedGet<SdfTimeSampleMap>();
	SdfTimeSampleMap timeSamples;
	TF_FOR_ALL(j, timeSampleMap)
	{
		auto v = queryTimeSample( path, j->first );
		timeSamples[j->first] = v;
	}
	return VtValue( timeSamples );
}

bool SceneCacheData::HasSpecAndField( const SdfPath &path, const TfToken &fieldName, SdfAbstractDataValue *value, SdfSpecType *specType) const
{
	VtValue const v = GetSpecTypeAndFieldValue(path, fieldName, specType);
	if ( v != g_empty )
	{
		return !value || value->StoreValue(v);
	}
	return false;
}

bool SceneCacheData::HasSpecAndField( const SdfPath &path, const TfToken &fieldName, VtValue *value, SdfSpecType *specType) const
{
	VtValue const v = GetSpecTypeAndFieldValue(path, fieldName, specType);
	if ( v != g_empty )
	{
		if (value)
		{
			*value = v;
		}
		return true;
	}
	return false;
}

const VtValue SceneCacheData::GetSpecTypeAndFieldValue(const SdfPath& path, const TfToken& field, SdfSpecType* specType) const
{
	HashTable::const_iterator i = m_data.find(path);
	if (i == m_data.end())
	{
		*specType = SdfSpecTypeUnknown;
	}
	else
	{
		const SpecData &spec = i->second;
		*specType = spec.specType;
		for (auto const &f: spec.fields)
		{
			if (f.first == field)
			{
				if (f.second.IsHolding<SdfTimeSampleMap>())
				{
					return getTimeSampleMap( path, field, f.second );
				}
				return f.second;
			}
		}
	}
	return g_empty;
}

const VtValue SceneCacheData::GetFieldValue(const SdfPath &path, const TfToken &field, bool loadTimeSampleMap) const
{
	HashTable::const_iterator i = m_data.find(path);
	if (i != m_data.end())
	{
		const SpecData & spec = i->second;
		for (auto const &f: spec.fields)
		{
			if (f.first == field)
			{
				if (f.second.IsHolding<SdfTimeSampleMap>() && loadTimeSampleMap )
				{
					return getTimeSampleMap( path, field, f.second );
				}
				return f.second;
			}
		}
	}
	return g_empty;
}

VtValue* SceneCacheData::GetMutableFieldValue(const SdfPath &path, const TfToken &field)
{
	HashTable::iterator i = m_data.find(path);
	if (i != m_data.end())
	{
		SpecData &spec = i->second;
		for (size_t j=0, jEnd = spec.fields.size(); j != jEnd; ++j)
		{
			if (spec.fields[j].first == field) {
				return &spec.fields[j].second;
			}
		}
	}
	return nullptr;
}

VtValue SceneCacheData::Get(const SdfPath &path, const TfToken & field) const
{
	
	return GetFieldValue(path, field);
}

void SceneCacheData::Set(const SdfPath &path, const TfToken & field, const VtValue& value)
{
	TfAutoMallocTag2 tag("Sdf", "SceneCacheData::Set");

	if (value.IsEmpty())
	{
		Erase(path, field);
		return;
	}

	VtValue* newValue = GetOrCreateFieldValue(path, field);
	if (newValue)
	{
		*newValue = value;
	}
}

void SceneCacheData::Set(const SdfPath &path, const TfToken &field, const SdfAbstractDataConstValue& value)
{
	TfAutoMallocTag2 tag("Sdf", "SceneCacheData::Set");

	VtValue* newValue = GetOrCreateFieldValue(path, field);
	if (newValue)
	{
		value.GetValue(newValue);
	}
}

VtValue* SceneCacheData::GetOrCreateFieldValue(const SdfPath &path, const TfToken &field)
{
	HashTable::iterator i = m_data.find(path);
	if (!TF_VERIFY(i != m_data.end(), "No spec at <%s> when trying to set field '%s'", path.GetText(), field.GetText()))
	{
		return nullptr;
	}

	SpecData &spec = i->second;
	for (auto &f: spec.fields)
	{
		if (f.first == field)
		{
			return &f.second;
		}
	}

	spec.fields.emplace_back(std::piecewise_construct, std::forward_as_tuple(field), std::forward_as_tuple());

	return &spec.fields.back().second;
}

void SceneCacheData::Erase(const SdfPath &path, const TfToken & field)
{
	HashTable::iterator i = m_data.find(path);
	if (i == m_data.end())
	{
		return;
	}
	
	SpecData &spec = i->second;
	for (size_t j=0, jEnd = spec.fields.size(); j != jEnd; ++j)
	{
		if (spec.fields[j].first == field)
		{
			spec.fields.erase(spec.fields.begin()+j);
			return;
		}
	}
}

std::vector<TfToken> SceneCacheData::List(const SdfPath &path) const
{
	HashTable::const_iterator i = m_data.find(path);
	if (i != m_data.end())
	{
		const SpecData & spec = i->second;

		std::vector<TfToken> names;
		names.reserve(spec.fields.size());
		for (size_t j=0, jEnd = spec.fields.size(); j != jEnd; ++j)
		{
			names.push_back(spec.fields[j].first);
		}
		return names;
	}

	return std::vector<TfToken>();
}

std::set<double> SceneCacheData::ListAllTimeSamples() const
{
	// Use a set to determine unique times.
	std::set<double> times;

	TF_FOR_ALL(i, m_data)
	{
		std::set<double> timesForPath = ListTimeSamplesForPath(i->first);
		times.insert(timesForPath.begin(), timesForPath.end());
	}

	return times;
}

std::set<double> SceneCacheData::ListTimeSamplesForPath(const SdfPath &path) const
{

	std::set<double> times;

	VtValue value = Get(path, SdfDataTokens->TimeSamples);
	if (value.IsHolding<SdfTimeSampleMap>())
	{
		const SdfTimeSampleMap & timeSampleMap =
			value.UncheckedGet<SdfTimeSampleMap>();
		TF_FOR_ALL(j, timeSampleMap)
		{
			times.insert(j->first);
		}
	}
	return times;
}

template <class Container, class GetTime>
static bool _GetBracketingTimeSamplesImpl( const Container &samples, const GetTime &getTime, const double time, double* tLower, double* tUpper)
{
	if (samples.empty())
	{
		// No samples.
		return false;
	}
	else if (time <= getTime(*samples.begin()))
	{
		// Time is at-or-before the first sample.
		*tLower = *tUpper = getTime(*samples.begin());
	}
	else if (time >= getTime(*samples.rbegin()))
	{
		// Time is at-or-after the last sample.
		*tLower = *tUpper = getTime(*samples.rbegin());
	}
	else
	{
		auto iter = samples.lower_bound(time);
		if (abs( getTime( *iter ) - time  ) <= 0.001)
		{
			// Time is exactly on a sample.
			*tLower = *tUpper = getTime(*iter);
		}
		else
		{
			// Time is in-between samples; return the bracketing times.
			*tUpper = getTime(*iter);
			--iter;
			*tLower = getTime(*iter);
		}
	}
	return true;
}

static bool _GetBracketingTimeSamples(const std::set<double> &samples, double time, double *tLower, double *tUpper)
{
	return _GetBracketingTimeSamplesImpl(samples, [](double t) { return t; }, time, tLower, tUpper);
}

static bool _GetBracketingTimeSamples(const SdfTimeSampleMap &samples, double time, double *tLower, double *tUpper)
{
	return _GetBracketingTimeSamplesImpl( samples, [](SdfTimeSampleMap::value_type const &p) { return p.first; }, time, tLower, tUpper);
}

bool SceneCacheData::GetBracketingTimeSamples( double time, double* tLower, double* tUpper) const
{
	return _GetBracketingTimeSamples( ListAllTimeSamples(), time, tLower, tUpper);
}

size_t SceneCacheData::GetNumTimeSamplesForPath(const SdfPath &path) const
{
	const VtValue fval = GetFieldValue(path, SdfDataTokens->TimeSamples, false);
	if ( fval != g_empty )
	{
		if ( fval.IsHolding<SdfTimeSampleMap>())
		{
			return fval.UncheckedGet<SdfTimeSampleMap>().size();
		}
	}
	return 0;
}

bool SceneCacheData::GetBracketingTimeSamplesForPath( const SdfPath &path, double time, double* tLower, double* tUpper) const
{
	const VtValue fval = GetFieldValue(path, SdfDataTokens->TimeSamples, false);
	if (fval != g_empty && fval.IsHolding<SdfTimeSampleMap>())
	{
		auto const &tsmap = fval.UncheckedGet<SdfTimeSampleMap>();
		return _GetBracketingTimeSamples(tsmap, time, tLower, tUpper);
	}
	return false;
}

const VtValue SceneCacheData::queryTimeSample( const SdfPath &path, double time ) const
{
	auto scenePath = m_internalPaths.at( USDScene::fromUSD( path.GetParentPath() ) );
	auto currentScene = m_scene->scene( scenePath, SceneInterface::MissingBehaviour::NullIfMissing );
	// ignore collection path
	if ( !currentScene )
	{
		return g_empty;
	}

	pxr::TfToken attributeName = path.GetNameToken();
	IECore::InternedString cortexAttributeName;
	if( boost::starts_with( attributeName.GetString(), "primvars:" ) )
	{
		cortexAttributeName = AttributeAlgo::nameFromUSD( { pxr::TfToken( attributeName.GetString().substr( 9 ) ), true } );
	}
	else
	{
		cortexAttributeName = AttributeAlgo::nameFromUSD( { attributeName, false } );
	}

	if ( attributeName == g_xformTransform )
	{
		auto transform = currentScene->readTransformAsMatrix( frameToTime( time ) );
		return VtValue( DataAlgo::toUSD( transform ) );
	}
	else if ( attributeName == UsdGeomTokens->extent )
	{
		auto bound = currentScene->readBound( frameToTime( time ) );
		VtArray<GfVec3f> extent;
		extent.push_back( DataAlgo::toUSD( V3f( bound.min ) ) );
		extent.push_back( DataAlgo::toUSD( V3f( bound.max ) ) );
		return VtValue( extent );
	}
	else if ( attributeName == UsdGeomTokens->visibility )
	{
		if ( currentScene->hasAttribute( SceneInterface::visibilityName ) )
		{
			if ( runTimeCast<const BoolData>( currentScene->readAttribute( SceneInterface::visibilityName, frameToTime( time ) ) )->readable() )
			{
				return VtValue( UsdGeomTokens->inherited );
			}
			else
			{
				return VtValue( UsdGeomTokens->invisible );
			}
		}
		else
		{
			return VtValue( UsdGeomTokens->inherited );
		}
	}
	else if ( currentScene->hasAttribute( cortexAttributeName ) )
	{
		if ( auto attributeValue = currentScene->readAttribute( cortexAttributeName, frameToTime( time ) ) )
		{
			auto usdAttribute = DataAlgo::toUSD( runTimeCast<const Data>( attributeValue.get() ) );
			return VtValue( usdAttribute );
		}
	}
	else
	{
		ConstObjectPtr object;
		try
		{
			object = currentScene->readObject( frameToTime( time ) );
		}
			catch( ... )
		{
			return g_empty;
		}
		if ( attributeName == UsdGeomTokens->points )
		{
			if ( auto primitive = dynamic_cast<const Primitive *>( object.get() ) )
			{
				PrimitiveVariableMap::const_iterator pointsPrimVarIt = primitive->variables.find( "P" );
				if( pointsPrimVarIt != primitive->variables.end() )
				{
					auto usdPoints = PrimitiveAlgo::toUSDExpanded( pointsPrimVarIt->second );
					return VtValue( usdPoints );
				}
			}
		}
		else if ( attributeName == UsdGeomTokens->primvarsDisplayColor )
		{
			if ( auto primitive = dynamic_cast<const Primitive *>( object.get() ) )
			{
				PrimitiveVariableMap::const_iterator csPrimVarIt = primitive->variables.find( g_csPrimVar );
				if( csPrimVarIt != primitive->variables.end() )
				{
					auto usdCs = DataAlgo::toUSD( csPrimVarIt->second.data.get() );
					return VtValue( usdCs );
				}
			}
		}
		else if ( attributeName == g_stPrimVar )
		{
			if ( auto primitive = dynamic_cast<const Primitive *>( object.get() ) )
			{
				PrimitiveVariableMap::const_iterator uvPrimVarIt = primitive->variables.find( g_uvPrimVar );
				if( uvPrimVarIt != primitive->variables.end() )
				{
					auto usdST = DataAlgo::toUSD( uvPrimVarIt->second.data.get() );
					return VtValue( usdST );
				}
			}
		}
		else if ( attributeName == g_stIndicesPrimVar )
		{
			if ( auto primitive = dynamic_cast<const Primitive *>( object.get() ) )
			{
				PrimitiveVariableMap::const_iterator uvPrimVarIt = primitive->variables.find( g_uvPrimVar );
				if( uvPrimVarIt != primitive->variables.end() )
				{
					if ( auto uvIndices = uvPrimVarIt->second.indices )
					{
						auto usdStIndices = DataAlgo::toUSD( uvIndices.get() );
						return VtValue( usdStIndices );
					}
					else
					{
						IntVectorDataPtr identityUvIndicesData = new IntVectorData();
						std::vector<int> &identityUvIndices = identityUvIndicesData->writable();
						for ( unsigned int i=0; i < primitive->variableSize( uvPrimVarIt->second.interpolation ); i++ )
						{
							identityUvIndices.push_back( i );
						}
						return VtValue( DataAlgo::toUSD( identityUvIndicesData.get() ) );
					}
				}
			}
		}
		else if ( attributeName == UsdGeomTokens->normals )
		{
			if ( auto primitive = dynamic_cast<const Primitive *>( object.get() ) )
			{
				PrimitiveVariableMap::const_iterator normalsPrimVarIt = primitive->variables.find( "N" );
				if( normalsPrimVarIt != primitive->variables.end() )
				{
					auto usdNormals = PrimitiveAlgo::toUSDExpanded( normalsPrimVarIt->second );
					return VtValue( usdNormals );
				}
			}
		}
		else if ( attributeName == g_NormalsIndicesPrimVar )
		{
			if ( auto primitive = dynamic_cast<const Primitive *>( object.get() ) )
			{
				PrimitiveVariableMap::const_iterator normalsPrimVarIt = primitive->variables.find( "N" );
				if( normalsPrimVarIt != primitive->variables.end() )
				{
					if ( auto normalsIndices = normalsPrimVarIt->second.indices )
					{
						auto usdStIndices = DataAlgo::toUSD( normalsIndices.get() );
						return VtValue( usdStIndices );
					}
					else
					{
						IntVectorDataPtr identityNormalsIndicesData = new IntVectorData();
						std::vector<int> &identityNormalsIndices = identityNormalsIndicesData->writable();
						for ( unsigned int i=0; i < primitive->variableSize( normalsPrimVarIt->second.interpolation ); i++ )
						{
							identityNormalsIndices.push_back( i );
						}
						return VtValue( DataAlgo::toUSD( identityNormalsIndicesData.get() ) );
					}
				}
			}
		}
		else if ( attributeName == UsdGeomTokens->faceVertexCounts )
		{
			if ( auto mesh = dynamic_cast<const MeshPrimitive *>( object.get() ) )
			{
				auto usdFaceVertexCounts = DataAlgo::toUSD( mesh->verticesPerFace() );
				return VtValue( usdFaceVertexCounts );
			}
		}
		else if ( attributeName == UsdGeomTokens->curveVertexCounts )
		{
			if ( auto curves = dynamic_cast<const CurvesPrimitive *>( object.get() ) )
			{
				auto usdCurveVertexCounts = DataAlgo::toUSD( curves->verticesPerCurve() );
				return VtValue( usdCurveVertexCounts );
			}
		}
		else if ( attributeName == UsdGeomTokens->basis )
		{
			if ( auto curves = dynamic_cast<const CurvesPrimitive *>( object.get() ) )
			{
				TfToken basis;
				if( curves->basis() == CubicBasisf::bezier() )
				{
					basis = UsdGeomTokens->bezier;
				}
				else if( curves->basis() == CubicBasisf::bSpline() )
				{
					basis = UsdGeomTokens->bspline;
				}
				else if( curves->basis() == CubicBasisf::catmullRom() )
				{
					basis = UsdGeomTokens->catmullRom;
				}
				else if ( curves->basis() != CubicBasisf::linear() )
				{
					IECore::msg( IECore::Msg::Warning, "SceneCacheData", "Unsupported basis" );
				}

				if( !basis.IsEmpty() )
				{
					return VtValue( basis );
				}
			}
		}
		else if ( attributeName == UsdGeomTokens->type )
		{
			if ( auto curves = dynamic_cast<const CurvesPrimitive *>( object.get() ) )
			{
				if( curves->basis() == CubicBasisf::linear() )
				{
					return VtValue( UsdGeomTokens->linear );
				}
				else
				{
					return VtValue( UsdGeomTokens->cubic );
				}
			}
		}
		else if ( attributeName == UsdGeomTokens->widths )
		{
			if ( auto primitive = dynamic_cast<const Primitive *>( object.get() ) )
			{
				PrimitiveVariableMap::const_iterator widthPrimVarIt = primitive->variables.find( g_widthPrimVar );
				if( widthPrimVarIt != primitive->variables.end() )
				{
					auto usdwidth = PrimitiveAlgo::toUSDExpanded( widthPrimVarIt->second );
					return VtValue( usdwidth );
				}
			}
		}
		else if ( attributeName == UsdGeomTokens->focalLength )
		{
			if ( auto camera = dynamic_cast<const Camera *>( object.get() ) )
			{
				float scale = 10.0f * camera->getFocalLengthWorldScale();
				auto usdFocal = DataAlgo::toUSD( camera->getFocalLength() * scale );
				return VtValue( usdFocal );
			}
		}
		else if ( attributeName == UsdGeomTokens->horizontalAperture )
		{
			if ( auto camera = dynamic_cast<const Camera *>( object.get() ) )
			{
				float scale = 10.0f * camera->getFocalLengthWorldScale();
				auto usdHorizontalAperture = DataAlgo::toUSD( camera->getAperture()[0] * scale );
				return VtValue( usdHorizontalAperture );
			}
		}
		else if ( attributeName == UsdGeomTokens->verticalAperture )
		{
			if ( auto camera = dynamic_cast<const Camera *>( object.get() ) )
			{
				float scale = 10.0f * camera->getFocalLengthWorldScale();
				auto usdverticalAperture = DataAlgo::toUSD( camera->getAperture()[1] * scale );
				return VtValue( usdverticalAperture );
			}
		}
		else if ( attributeName == UsdGeomTokens->horizontalApertureOffset )
		{
			if ( auto camera = dynamic_cast<const Camera *>( object.get() ) )
			{
				float scale = 10.0f * camera->getFocalLengthWorldScale();
				auto usdHorizontalApertureOffset = DataAlgo::toUSD( camera->getApertureOffset()[0] * scale );
				return VtValue( usdHorizontalApertureOffset );
			}
		}
		else if ( attributeName == UsdGeomTokens->verticalApertureOffset )
		{
			if ( auto camera = dynamic_cast<const Camera *>( object.get() ) )
			{
				float scale = 10.0f * camera->getFocalLengthWorldScale();
				auto usdverticalApertureOffset = DataAlgo::toUSD( camera->getApertureOffset()[1] * scale );
				return VtValue( usdverticalApertureOffset );
			}
		}
		else if ( attributeName == UsdGeomTokens->faceVertexIndices )
		{
			if ( auto mesh = dynamic_cast<const MeshPrimitive *>( object.get() ) )
			{
				auto usdVerticesPerFace = DataAlgo::toUSD( mesh->vertexIds() );
				return VtValue( usdVerticesPerFace );
			}
		}
		else if ( attributeName == UsdGeomTokens->cornerIndices )
		{
			if ( auto mesh = dynamic_cast<const MeshPrimitive *>( object.get() ) )
			{
				auto usdCornerIndices = DataAlgo::toUSD( mesh->cornerIds() );
				return VtValue( usdCornerIndices );
			}
		}
		else if ( attributeName == UsdGeomTokens->cornerSharpnesses )
		{
			if ( auto mesh = dynamic_cast<const MeshPrimitive *>( object.get() ) )
			{
				auto usdCornerSharpnesses = DataAlgo::toUSD( mesh->cornerSharpnesses() );
				return VtValue( usdCornerSharpnesses );
			}
		}
		else if ( attributeName == UsdGeomTokens->creaseIndices )
		{
			if ( auto mesh = dynamic_cast<const MeshPrimitive *>( object.get() ) )
			{
				auto usdCreaseIndices = DataAlgo::toUSD( mesh->creaseIds() );
				return VtValue( usdCreaseIndices );
			}
		}
		else if ( attributeName == UsdGeomTokens->creaseLengths )
		{
			if ( auto mesh = dynamic_cast<const MeshPrimitive *>( object.get() ) )
			{
				auto usdCreaseLength = DataAlgo::toUSD( mesh->creaseLengths() );
				return VtValue( usdCreaseLength );
			}
		}
		else if ( attributeName == UsdGeomTokens->creaseSharpnesses )
		{
			if ( auto mesh = dynamic_cast<const MeshPrimitive *>( object.get() ) )
			{
				auto usdCreaseSharpnesses = DataAlgo::toUSD( mesh->creaseSharpnesses() );
				return VtValue( usdCreaseSharpnesses );
			}
		}
		else if ( auto primitive = dynamic_cast<const Primitive *>( object.get() ) )
		{
			static const std::string indicesSuffix = ":indices";
			bool indices = false;

			// remove prefix `primvars:`
			auto cleanAttribute = attributeName.GetString();
			boost::algorithm::erase_first( cleanAttribute, "primvars:" );

			if( boost::algorithm::ends_with( cleanAttribute, indicesSuffix ) )
			{
				indices = true;
				boost::algorithm::erase_last( cleanAttribute, indicesSuffix );
			}

			PrimitiveVariableMap::const_iterator customPrimVarIt = primitive->variables.find( cleanAttribute );
			if( customPrimVarIt != primitive->variables.end() )
			{
				if( indices )
				{
					if ( auto customIndices = customPrimVarIt->second.indices )
					{
						auto usdStIndices = DataAlgo::toUSD( customIndices.get() );
						return VtValue( usdStIndices );
					}
					else
					{
						IntVectorDataPtr identityCustomIndicesData = new IntVectorData();
						std::vector<int> &identityCustomIndices = identityCustomIndicesData->writable();
						for ( unsigned int i=0; i < primitive->variableSize( customPrimVarIt->second.interpolation ); i++ )
						{
							identityCustomIndices.push_back( i );
						}
						return VtValue( DataAlgo::toUSD( identityCustomIndicesData.get() ) );
					}
				}
				else
				{
					auto usdCustomPrimVar = PrimitiveAlgo::toUSDExpanded( customPrimVarIt->second );
					return VtValue( usdCustomPrimVar );
				}
			}
		}
	}
	return g_empty;
}

bool SceneCacheData::QueryTimeSample(const SdfPath &path, double time, VtValue *value) const
{
	auto result = queryTimeSample( path, time );
	if ( result != g_empty )
	{
		if ( value )
		{
			*value = result;
		}
		return true;
	}
	else
	{
		const VtValue fval = GetFieldValue(path, SdfDataTokens->TimeSamples);
		if (fval != g_empty && fval.IsHolding<SdfTimeSampleMap>())
		{
			auto const &tsmap = fval.UncheckedGet<SdfTimeSampleMap>();
			auto iter = tsmap.find(time);
			if (iter != tsmap.end())
			{
				if (value)
					*value = iter->second;
				return true;
			}
		}
	}
	return false;
}

bool SceneCacheData::QueryTimeSample(const SdfPath &path, double time, SdfAbstractDataValue* value) const
{
	auto result = queryTimeSample( path, time );
	if ( result != g_empty )
	{
		return !value || value->StoreValue( result );
	}
	else
	{
		const VtValue fval = GetFieldValue(path, SdfDataTokens->TimeSamples);
		if (fval != g_empty && fval.IsHolding<SdfTimeSampleMap>())
		{
			auto const &tsmap = fval.UncheckedGet<SdfTimeSampleMap>();
			auto iter = tsmap.find(time);
			if (iter != tsmap.end())
			{
				return !value || value->StoreValue(iter->second);
			}
		}
	}
	return false;
}

void SceneCacheData::SetTimeSample(const SdfPath &path, double time, const VtValue& value)
{
	if (value.IsEmpty())
	{
		EraseTimeSample(path, time);
		return;
	}

	SdfTimeSampleMap newSamples;

	// Attempt to get a pointer to an existing timeSamples field.
	VtValue *fieldValue = GetMutableFieldValue(path, SdfDataTokens->TimeSamples);

	// If we have one, swap it out so we can modify it.
	if (fieldValue && fieldValue->IsHolding<SdfTimeSampleMap>())
	{
		fieldValue->UncheckedSwap(newSamples);
	}
	
	// Insert or overwrite into newSamples.
	newSamples[time] = value;

	// Set back into the field.
	if (fieldValue)
	{
		fieldValue->Swap(newSamples);
	}
	else
	{
		Set(path, SdfDataTokens->TimeSamples, VtValue::Take(newSamples));
	}
}

void SceneCacheData::EraseTimeSample(const SdfPath &path, double time)
{
	SdfTimeSampleMap newSamples;

	// Attempt to get a pointer to an existing timeSamples field.
	VtValue *fieldValue = GetMutableFieldValue(path, SdfDataTokens->TimeSamples);

	// If we have one, swap it out so we can modify it.  If we do not have one,
	// there's nothing to erase so we're done.
	if (fieldValue && fieldValue->IsHolding<SdfTimeSampleMap>())
	{
		fieldValue->UncheckedSwap(newSamples);
	}
	else
	{
		return;
	}
	
	// Erase from newSamples.
	newSamples.erase(time);

	// Check to see if the result is empty.  In that case we remove the field.
	if (newSamples.empty())
	{
		Erase(path, SdfDataTokens->TimeSamples);
	}
	else
	{
		fieldValue->UncheckedSwap(newSamples);
	}
}
