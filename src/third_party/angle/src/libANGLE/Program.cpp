//
// Copyright (c) 2002-2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Program.cpp: Implements the gl::Program class. Implements GL program objects
// and related functionality. [OpenGL ES 2.0.24] section 2.10.3 page 28.

#include "libANGLE/Program.h"

#include <algorithm>

#include "common/bitset_utils.h"
#include "common/debug.h"
#include "common/platform.h"
#include "common/utilities.h"
#include "common/version.h"
#include "compiler/translator/blocklayout.h"
#include "libANGLE/Context.h"
#include "libANGLE/ResourceManager.h"
#include "libANGLE/features.h"
#include "libANGLE/renderer/GLImplFactory.h"
#include "libANGLE/renderer/ProgramImpl.h"
#include "libANGLE/VaryingPacking.h"
#include "libANGLE/queryconversions.h"
#include "libANGLE/Uniform.h"
#include "libANGLE/UniformLinker.h"

namespace gl
{

namespace
{

void WriteShaderVar(BinaryOutputStream *stream, const sh::ShaderVariable &var)
{
    stream->writeInt(var.type);
    stream->writeInt(var.precision);
    stream->writeString(var.name);
    stream->writeString(var.mappedName);
    stream->writeInt(var.arraySize);
    stream->writeInt(var.staticUse);
    stream->writeString(var.structName);
    ASSERT(var.fields.empty());
}

void LoadShaderVar(BinaryInputStream *stream, sh::ShaderVariable *var)
{
    var->type       = stream->readInt<GLenum>();
    var->precision  = stream->readInt<GLenum>();
    var->name       = stream->readString();
    var->mappedName = stream->readString();
    var->arraySize  = stream->readInt<unsigned int>();
    var->staticUse  = stream->readBool();
    var->structName = stream->readString();
}

// This simplified cast function doesn't need to worry about advanced concepts like
// depth range values, or casting to bool.
template <typename DestT, typename SrcT>
DestT UniformStateQueryCast(SrcT value);

// From-Float-To-Integer Casts
template <>
GLint UniformStateQueryCast(GLfloat value)
{
    return clampCast<GLint>(roundf(value));
}

template <>
GLuint UniformStateQueryCast(GLfloat value)
{
    return clampCast<GLuint>(roundf(value));
}

// From-Integer-to-Integer Casts
template <>
GLint UniformStateQueryCast(GLuint value)
{
    return clampCast<GLint>(value);
}

template <>
GLuint UniformStateQueryCast(GLint value)
{
    return clampCast<GLuint>(value);
}

// From-Boolean-to-Anything Casts
template <>
GLfloat UniformStateQueryCast(GLboolean value)
{
    return (value == GL_TRUE ? 1.0f : 0.0f);
}

template <>
GLint UniformStateQueryCast(GLboolean value)
{
    return (value == GL_TRUE ? 1 : 0);
}

template <>
GLuint UniformStateQueryCast(GLboolean value)
{
    return (value == GL_TRUE ? 1u : 0u);
}

// Default to static_cast
template <typename DestT, typename SrcT>
DestT UniformStateQueryCast(SrcT value)
{
    return static_cast<DestT>(value);
}

template <typename SrcT, typename DestT>
void UniformStateQueryCastLoop(DestT *dataOut, const uint8_t *srcPointer, int components)
{
    for (int comp = 0; comp < components; ++comp)
    {
        // We only work with strides of 4 bytes for uniform components. (GLfloat/GLint)
        // Don't use SrcT stride directly since GLboolean has a stride of 1 byte.
        size_t offset               = comp * 4;
        const SrcT *typedSrcPointer = reinterpret_cast<const SrcT *>(&srcPointer[offset]);
        dataOut[comp]               = UniformStateQueryCast<DestT>(*typedSrcPointer);
    }
}

// true if varying x has a higher priority in packing than y
bool ComparePackedVarying(const PackedVarying &x, const PackedVarying &y)
{
    // If the PackedVarying 'x' or 'y' to be compared is an array element, this clones an equivalent
    // non-array shader variable 'vx' or 'vy' for actual comparison instead.
    sh::ShaderVariable vx, vy;
    const sh::ShaderVariable *px, *py;
    if (x.isArrayElement())
    {
        vx           = *x.varying;
        vx.arraySize = 0;
        px           = &vx;
    }
    else
    {
        px = x.varying;
    }

    if (y.isArrayElement())
    {
        vy           = *y.varying;
        vy.arraySize = 0;
        py           = &vy;
    }
    else
    {
        py = y.varying;
    }

    return gl::CompareShaderVar(*px, *py);
}

template <typename VarT>
GLuint GetResourceIndexFromName(const std::vector<VarT> &list, const std::string &name)
{
    size_t subscript     = GL_INVALID_INDEX;
    std::string baseName = ParseResourceName(name, &subscript);

    // The app is not allowed to specify array indices other than 0 for arrays of basic types
    if (subscript != 0 && subscript != GL_INVALID_INDEX)
    {
        return GL_INVALID_INDEX;
    }

    for (size_t index = 0; index < list.size(); index++)
    {
        const VarT &resource = list[index];
        if (resource.name == baseName)
        {
            if (resource.isArray() || subscript == GL_INVALID_INDEX)
            {
                return static_cast<GLuint>(index);
            }
        }
    }

    return GL_INVALID_INDEX;
}

void CopyStringToBuffer(GLchar *buffer, const std::string &string, GLsizei bufSize, GLsizei *length)
{
    ASSERT(bufSize > 0);
    strncpy(buffer, string.c_str(), bufSize);
    buffer[bufSize - 1] = '\0';

    if (length)
    {
        *length = static_cast<GLsizei>(strlen(buffer));
    }
}

bool IncludeSameArrayElement(const std::set<std::string> &nameSet, const std::string &name)
{
    size_t subscript     = GL_INVALID_INDEX;
    std::string baseName = ParseResourceName(name, &subscript);
    for (auto it = nameSet.begin(); it != nameSet.end(); ++it)
    {
        size_t arrayIndex     = GL_INVALID_INDEX;
        std::string arrayName = ParseResourceName(*it, &arrayIndex);
        if (baseName == arrayName && (subscript == GL_INVALID_INDEX ||
                                      arrayIndex == GL_INVALID_INDEX || subscript == arrayIndex))
        {
            return true;
        }
    }
    return false;
}

}  // anonymous namespace

const char *const g_fakepath = "C:\\fakepath";

InfoLog::InfoLog()
{
}

InfoLog::~InfoLog()
{
}

size_t InfoLog::getLength() const
{
    const std::string &logString = mStream.str();
    return logString.empty() ? 0 : logString.length() + 1;
}

void InfoLog::getLog(GLsizei bufSize, GLsizei *length, char *infoLog) const
{
    size_t index = 0;

    if (bufSize > 0)
    {
        const std::string str(mStream.str());

        if (!str.empty())
        {
            index = std::min(static_cast<size_t>(bufSize) - 1, str.length());
            memcpy(infoLog, str.c_str(), index);
        }

        infoLog[index] = '\0';
    }

    if (length)
    {
        *length = static_cast<GLsizei>(index);
    }
}

// append a santized message to the program info log.
// The D3D compiler includes a fake file path in some of the warning or error
// messages, so lets remove all occurrences of this fake file path from the log.
void InfoLog::appendSanitized(const char *message)
{
    std::string msg(message);

    size_t found;
    do
    {
        found = msg.find(g_fakepath);
        if (found != std::string::npos)
        {
            msg.erase(found, strlen(g_fakepath));
        }
    }
    while (found != std::string::npos);

    mStream << message << std::endl;
}

void InfoLog::reset()
{
}

VariableLocation::VariableLocation() : name(), element(0), index(0), used(false), ignored(false)
{
}

VariableLocation::VariableLocation(const std::string &name,
                                   unsigned int element,
                                   unsigned int index)
    : name(name), element(element), index(index), used(true), ignored(false)
{
}

void Program::Bindings::bindLocation(GLuint index, const std::string &name)
{
    mBindings[name] = index;
}

int Program::Bindings::getBinding(const std::string &name) const
{
    auto iter = mBindings.find(name);
    return (iter != mBindings.end()) ? iter->second : -1;
}

Program::Bindings::const_iterator Program::Bindings::begin() const
{
    return mBindings.begin();
}

Program::Bindings::const_iterator Program::Bindings::end() const
{
    return mBindings.end();
}

ProgramState::ProgramState()
    : mLabel(),
      mAttachedFragmentShader(nullptr),
      mAttachedVertexShader(nullptr),
      mAttachedComputeShader(nullptr),
      mTransformFeedbackBufferMode(GL_INTERLEAVED_ATTRIBS),
      mSamplerUniformRange(0, 0),
      mBinaryRetrieveableHint(false)
{
    mComputeShaderLocalSize.fill(1);
}

ProgramState::~ProgramState()
{
    ASSERT(!mAttachedVertexShader && !mAttachedFragmentShader && !mAttachedComputeShader);
}

const std::string &ProgramState::getLabel()
{
    return mLabel;
}

GLint ProgramState::getUniformLocation(const std::string &name) const
{
    size_t subscript     = GL_INVALID_INDEX;
    std::string baseName = ParseResourceName(name, &subscript);

    for (size_t location = 0; location < mUniformLocations.size(); ++location)
    {
        const VariableLocation &uniformLocation = mUniformLocations[location];
        if (!uniformLocation.used)
        {
            continue;
        }

        const LinkedUniform &uniform = mUniforms[uniformLocation.index];

        if (uniform.name == baseName)
        {
            if (uniform.isArray())
            {
                if (uniformLocation.element == subscript ||
                    (uniformLocation.element == 0 && subscript == GL_INVALID_INDEX))
                {
                    return static_cast<GLint>(location);
                }
            }
            else
            {
                if (subscript == GL_INVALID_INDEX)
                {
                    return static_cast<GLint>(location);
                }
            }
        }
    }

    return -1;
}

GLuint ProgramState::getUniformIndexFromName(const std::string &name) const
{
    return GetResourceIndexFromName(mUniforms, name);
}

GLuint ProgramState::getUniformIndexFromLocation(GLint location) const
{
    ASSERT(location >= 0 && static_cast<size_t>(location) < mUniformLocations.size());
    return mUniformLocations[location].index;
}

Optional<GLuint> ProgramState::getSamplerIndex(GLint location) const
{
    GLuint index = getUniformIndexFromLocation(location);
    if (!isSamplerUniformIndex(index))
    {
        return Optional<GLuint>::Invalid();
    }

    return getSamplerIndexFromUniformIndex(index);
}

bool ProgramState::isSamplerUniformIndex(GLuint index) const
{
    return index >= mSamplerUniformRange.start && index < mSamplerUniformRange.end;
}

GLuint ProgramState::getSamplerIndexFromUniformIndex(GLuint uniformIndex) const
{
    ASSERT(isSamplerUniformIndex(uniformIndex));
    return uniformIndex - mSamplerUniformRange.start;
}

Program::Program(rx::GLImplFactory *factory, ShaderProgramManager *manager, GLuint handle)
    : mProgram(factory->createProgram(mState)),
      mValidated(false),
      mLinked(false),
      mDeleteStatus(false),
      mRefCount(0),
      mResourceManager(manager),
      mHandle(handle)
{
    ASSERT(mProgram);

    resetUniformBlockBindings();
    unlink();
}

Program::~Program()
{
    ASSERT(!mState.mAttachedVertexShader && !mState.mAttachedFragmentShader &&
           !mState.mAttachedComputeShader);
    SafeDelete(mProgram);
}

void Program::destroy(const Context *context)
{
    if (mState.mAttachedVertexShader != nullptr)
    {
        mState.mAttachedVertexShader->release(context);
        mState.mAttachedVertexShader = nullptr;
    }

    if (mState.mAttachedFragmentShader != nullptr)
    {
        mState.mAttachedFragmentShader->release(context);
        mState.mAttachedFragmentShader = nullptr;
    }

    if (mState.mAttachedComputeShader != nullptr)
    {
        mState.mAttachedComputeShader->release(context);
        mState.mAttachedComputeShader = nullptr;
    }

    mProgram->destroy(rx::SafeGetImpl(context));
}

void Program::setLabel(const std::string &label)
{
    mState.mLabel = label;
}

const std::string &Program::getLabel() const
{
    return mState.mLabel;
}

void Program::attachShader(Shader *shader)
{
    switch (shader->getType())
    {
        case GL_VERTEX_SHADER:
        {
            ASSERT(!mState.mAttachedVertexShader);
            mState.mAttachedVertexShader = shader;
            mState.mAttachedVertexShader->addRef();
            break;
        }
        case GL_FRAGMENT_SHADER:
        {
            ASSERT(!mState.mAttachedFragmentShader);
            mState.mAttachedFragmentShader = shader;
            mState.mAttachedFragmentShader->addRef();
            break;
        }
        case GL_COMPUTE_SHADER:
        {
            ASSERT(!mState.mAttachedComputeShader);
            mState.mAttachedComputeShader = shader;
            mState.mAttachedComputeShader->addRef();
            break;
        }
        default:
            UNREACHABLE();
    }
}

bool Program::detachShader(const Context *context, Shader *shader)
{
    switch (shader->getType())
    {
        case GL_VERTEX_SHADER:
        {
            if (mState.mAttachedVertexShader != shader)
            {
                return false;
            }

            shader->release(context);
            mState.mAttachedVertexShader = nullptr;
            break;
        }
        case GL_FRAGMENT_SHADER:
        {
            if (mState.mAttachedFragmentShader != shader)
            {
                return false;
            }

            shader->release(context);
            mState.mAttachedFragmentShader = nullptr;
            break;
        }
        case GL_COMPUTE_SHADER:
        {
            if (mState.mAttachedComputeShader != shader)
            {
                return false;
            }

            shader->release(context);
            mState.mAttachedComputeShader = nullptr;
            break;
        }
        default:
            UNREACHABLE();
    }

    return true;
}

int Program::getAttachedShadersCount() const
{
    return (mState.mAttachedVertexShader ? 1 : 0) + (mState.mAttachedFragmentShader ? 1 : 0) +
           (mState.mAttachedComputeShader ? 1 : 0);
}

void Program::bindAttributeLocation(GLuint index, const char *name)
{
    mAttributeBindings.bindLocation(index, name);
}

void Program::bindUniformLocation(GLuint index, const char *name)
{
    // Bind the base uniform name only since array indices other than 0 cannot be bound
    mUniformLocationBindings.bindLocation(index, ParseResourceName(name, nullptr));
}

void Program::bindFragmentInputLocation(GLint index, const char *name)
{
    mFragmentInputBindings.bindLocation(index, name);
}

BindingInfo Program::getFragmentInputBindingInfo(GLint index) const
{
    BindingInfo ret;
    ret.type  = GL_NONE;
    ret.valid = false;

    const Shader *fragmentShader = mState.getAttachedFragmentShader();
    ASSERT(fragmentShader);

    // Find the actual fragment shader varying we're interested in
    const std::vector<sh::Varying> &inputs = fragmentShader->getVaryings();

    for (const auto &binding : mFragmentInputBindings)
    {
        if (binding.second != static_cast<GLuint>(index))
            continue;

        ret.valid = true;

        std::string originalName = binding.first;
        unsigned int arrayIndex  = ParseAndStripArrayIndex(&originalName);

        for (const auto &in : inputs)
        {
            if (in.name == originalName)
            {
                if (in.isArray())
                {
                    // The client wants to bind either "name" or "name[0]".
                    // GL ES 3.1 spec refers to active array names with language such as:
                    // "if the string identifies the base name of an active array, where the
                    // string would exactly match the name of the variable if the suffix "[0]"
                    // were appended to the string".
                    if (arrayIndex == GL_INVALID_INDEX)
                        arrayIndex = 0;

                    ret.name = in.mappedName + "[" + ToString(arrayIndex) + "]";
                }
                else
                {
                    ret.name = in.mappedName;
                }
                ret.type = in.type;
                return ret;
            }
        }
    }

    return ret;
}

void Program::pathFragmentInputGen(GLint index,
                                   GLenum genMode,
                                   GLint components,
                                   const GLfloat *coeffs)
{
    // If the location is -1 then the command is silently ignored
    if (index == -1)
        return;

    const auto &binding = getFragmentInputBindingInfo(index);

    // If the input doesn't exist then then the command is silently ignored
    // This could happen through optimization for example, the shader translator
    // decides that a variable is not actually being used and optimizes it away.
    if (binding.name.empty())
        return;

    mProgram->setPathFragmentInputGen(binding.name, genMode, components, coeffs);
}

// The attached shaders are checked for linking errors by matching up their variables.
// Uniform, input and output variables get collected.
// The code gets compiled into binaries.
Error Program::link(const gl::Context *context)
{
    const auto &data = context->getContextState();

    unlink();

    mInfoLog.reset();
    resetUniformBlockBindings();

    const Caps &caps = data.getCaps();

    auto vertexShader   = mState.mAttachedVertexShader;
    auto fragmentShader = mState.mAttachedFragmentShader;
    auto computeShader  = mState.mAttachedComputeShader;

    bool isComputeShaderAttached   = (computeShader != nullptr);
    bool nonComputeShadersAttached = (vertexShader != nullptr || fragmentShader != nullptr);
    // Check whether we both have a compute and non-compute shaders attached.
    // If there are of both types attached, then linking should fail.
    // OpenGL ES 3.10, 7.3 Program Objects, under LinkProgram
    if (isComputeShaderAttached == true && nonComputeShadersAttached == true)
    {
        mInfoLog << "Both a compute and non-compute shaders are attached to the same program.";
        return NoError();
    }

    if (computeShader)
    {
        if (!computeShader->isCompiled())
        {
            mInfoLog << "Attached compute shader is not compiled.";
            return NoError();
        }
        ASSERT(computeShader->getType() == GL_COMPUTE_SHADER);

        mState.mComputeShaderLocalSize = computeShader->getWorkGroupSize();

        // GLSL ES 3.10, 4.4.1.1 Compute Shader Inputs
        // If the work group size is not specified, a link time error should occur.
        if (!mState.mComputeShaderLocalSize.isDeclared())
        {
            mInfoLog << "Work group size is not specified.";
            return NoError();
        }

        if (!linkUniforms(mInfoLog, caps, mUniformLocationBindings))
        {
            return NoError();
        }

        if (!linkUniformBlocks(mInfoLog, caps))
        {
            return NoError();
        }

        gl::VaryingPacking noPacking(0, PackMode::ANGLE_RELAXED);
        ANGLE_TRY_RESULT(mProgram->link(context->getImplementation(), noPacking, mInfoLog),
                         mLinked);
        if (!mLinked)
        {
            return NoError();
        }
    }
    else
    {
        if (!fragmentShader || !fragmentShader->isCompiled())
        {
            return NoError();
        }
        ASSERT(fragmentShader->getType() == GL_FRAGMENT_SHADER);

        if (!vertexShader || !vertexShader->isCompiled())
        {
            return NoError();
        }
        ASSERT(vertexShader->getType() == GL_VERTEX_SHADER);

        if (fragmentShader->getShaderVersion() != vertexShader->getShaderVersion())
        {
            mInfoLog << "Fragment shader version does not match vertex shader version.";
            return NoError();
        }

        if (!linkAttributes(data, mInfoLog))
        {
            return NoError();
        }

        if (!linkVaryings(mInfoLog))
        {
            return NoError();
        }

        if (!linkUniforms(mInfoLog, caps, mUniformLocationBindings))
        {
            return NoError();
        }

        if (!linkUniformBlocks(mInfoLog, caps))
        {
            return NoError();
        }

        const auto &mergedVaryings = getMergedVaryings();

        if (!linkValidateTransformFeedback(context, mInfoLog, mergedVaryings, caps))
        {
            return NoError();
        }

        linkOutputVariables();

        // Validate we can pack the varyings.
        std::vector<PackedVarying> packedVaryings = getPackedVaryings(mergedVaryings);

        // Map the varyings to the register file
        // In WebGL, we use a slightly different handling for packing variables.
        auto packMode = data.getExtensions().webglCompatibility ? PackMode::WEBGL_STRICT
                                                                : PackMode::ANGLE_RELAXED;
        VaryingPacking varyingPacking(data.getCaps().maxVaryingVectors, packMode);
        if (!varyingPacking.packUserVaryings(mInfoLog, packedVaryings,
                                             mState.getTransformFeedbackVaryingNames()))
        {
            return NoError();
        }

        ANGLE_TRY_RESULT(mProgram->link(context->getImplementation(), varyingPacking, mInfoLog),
                         mLinked);
        if (!mLinked)
        {
            return NoError();
        }

        gatherTransformFeedbackVaryings(mergedVaryings);
    }

    setUniformValuesFromBindingQualifiers();

    gatherInterfaceBlockInfo();

    return NoError();
}

// Returns the program object to an unlinked state, before re-linking, or at destruction
void Program::unlink()
{
    mState.mAttributes.clear();
    mState.mActiveAttribLocationsMask.reset();
    mState.mLinkedTransformFeedbackVaryings.clear();
    mState.mUniforms.clear();
    mState.mUniformLocations.clear();
    mState.mUniformBlocks.clear();
    mState.mOutputVariables.clear();
    mState.mOutputLocations.clear();
    mState.mComputeShaderLocalSize.fill(1);
    mState.mSamplerBindings.clear();

    mValidated = false;

    mLinked = false;
}

bool Program::isLinked() const
{
    return mLinked;
}

Error Program::loadBinary(const Context *context,
                          GLenum binaryFormat,
                          const void *binary,
                          GLsizei length)
{
    unlink();

#if ANGLE_PROGRAM_BINARY_LOAD != ANGLE_ENABLED
    return NoError();
#else
    ASSERT(binaryFormat == GL_PROGRAM_BINARY_ANGLE);
    if (binaryFormat != GL_PROGRAM_BINARY_ANGLE)
    {
        mInfoLog << "Invalid program binary format.";
        return NoError();
    }

    BinaryInputStream stream(binary, length);

    unsigned char commitString[ANGLE_COMMIT_HASH_SIZE];
    stream.readBytes(commitString, ANGLE_COMMIT_HASH_SIZE);
    if (memcmp(commitString, ANGLE_COMMIT_HASH, sizeof(unsigned char) * ANGLE_COMMIT_HASH_SIZE) !=
        0)
    {
        mInfoLog << "Invalid program binary version.";
        return NoError();
    }

    int majorVersion = stream.readInt<int>();
    int minorVersion = stream.readInt<int>();
    if (majorVersion != context->getClientMajorVersion() ||
        minorVersion != context->getClientMinorVersion())
    {
        mInfoLog << "Cannot load program binaries across different ES context versions.";
        return NoError();
    }

    mState.mComputeShaderLocalSize[0] = stream.readInt<int>();
    mState.mComputeShaderLocalSize[1] = stream.readInt<int>();
    mState.mComputeShaderLocalSize[2] = stream.readInt<int>();

    static_assert(MAX_VERTEX_ATTRIBS <= sizeof(unsigned long) * 8,
                  "Too many vertex attribs for mask");
    mState.mActiveAttribLocationsMask = stream.readInt<unsigned long>();

    unsigned int attribCount = stream.readInt<unsigned int>();
    ASSERT(mState.mAttributes.empty());
    for (unsigned int attribIndex = 0; attribIndex < attribCount; ++attribIndex)
    {
        sh::Attribute attrib;
        LoadShaderVar(&stream, &attrib);
        attrib.location = stream.readInt<int>();
        mState.mAttributes.push_back(attrib);
    }

    unsigned int uniformCount = stream.readInt<unsigned int>();
    ASSERT(mState.mUniforms.empty());
    for (unsigned int uniformIndex = 0; uniformIndex < uniformCount; ++uniformIndex)
    {
        LinkedUniform uniform;
        LoadShaderVar(&stream, &uniform);

        uniform.blockIndex                 = stream.readInt<int>();
        uniform.blockInfo.offset           = stream.readInt<int>();
        uniform.blockInfo.arrayStride      = stream.readInt<int>();
        uniform.blockInfo.matrixStride     = stream.readInt<int>();
        uniform.blockInfo.isRowMajorMatrix = stream.readBool();

        mState.mUniforms.push_back(uniform);
    }

    const unsigned int uniformIndexCount = stream.readInt<unsigned int>();
    ASSERT(mState.mUniformLocations.empty());
    for (unsigned int uniformIndexIndex = 0; uniformIndexIndex < uniformIndexCount;
         uniformIndexIndex++)
    {
        VariableLocation variable;
        stream.readString(&variable.name);
        stream.readInt(&variable.element);
        stream.readInt(&variable.index);
        stream.readBool(&variable.used);
        stream.readBool(&variable.ignored);

        mState.mUniformLocations.push_back(variable);
    }

    unsigned int uniformBlockCount = stream.readInt<unsigned int>();
    ASSERT(mState.mUniformBlocks.empty());
    for (unsigned int uniformBlockIndex = 0; uniformBlockIndex < uniformBlockCount;
         ++uniformBlockIndex)
    {
        UniformBlock uniformBlock;
        stream.readString(&uniformBlock.name);
        stream.readBool(&uniformBlock.isArray);
        stream.readInt(&uniformBlock.arrayElement);
        stream.readInt(&uniformBlock.dataSize);
        stream.readBool(&uniformBlock.vertexStaticUse);
        stream.readBool(&uniformBlock.fragmentStaticUse);

        unsigned int numMembers = stream.readInt<unsigned int>();
        for (unsigned int blockMemberIndex = 0; blockMemberIndex < numMembers; blockMemberIndex++)
        {
            uniformBlock.memberUniformIndexes.push_back(stream.readInt<unsigned int>());
        }

        mState.mUniformBlocks.push_back(uniformBlock);
    }

    for (GLuint bindingIndex = 0; bindingIndex < mState.mUniformBlockBindings.size();
         ++bindingIndex)
    {
        stream.readInt(&mState.mUniformBlockBindings[bindingIndex]);
        mState.mActiveUniformBlockBindings.set(bindingIndex,
                                               mState.mUniformBlockBindings[bindingIndex] != 0);
    }

    unsigned int transformFeedbackVaryingCount = stream.readInt<unsigned int>();
    ASSERT(mState.mLinkedTransformFeedbackVaryings.empty());
    for (unsigned int transformFeedbackVaryingIndex = 0;
        transformFeedbackVaryingIndex < transformFeedbackVaryingCount;
        ++transformFeedbackVaryingIndex)
    {
        sh::Varying varying;
        stream.readInt(&varying.arraySize);
        stream.readInt(&varying.type);
        stream.readString(&varying.name);

        GLuint arrayIndex = stream.readInt<GLuint>();

        mState.mLinkedTransformFeedbackVaryings.emplace_back(varying, arrayIndex);
    }

    stream.readInt(&mState.mTransformFeedbackBufferMode);

    unsigned int outputCount = stream.readInt<unsigned int>();
    ASSERT(mState.mOutputVariables.empty());
    for (unsigned int outputIndex = 0; outputIndex < outputCount; ++outputIndex)
    {
        sh::OutputVariable output;
        LoadShaderVar(&stream, &output);
        output.location = stream.readInt<int>();
        mState.mOutputVariables.push_back(output);
    }

    unsigned int outputVarCount = stream.readInt<unsigned int>();
    for (unsigned int outputIndex = 0; outputIndex < outputVarCount; ++outputIndex)
    {
        int locationIndex = stream.readInt<int>();
        VariableLocation locationData;
        stream.readInt(&locationData.element);
        stream.readInt(&locationData.index);
        stream.readString(&locationData.name);
        mState.mOutputLocations[locationIndex] = locationData;
    }

    stream.readInt(&mState.mSamplerUniformRange.start);
    stream.readInt(&mState.mSamplerUniformRange.end);

    unsigned int samplerCount = stream.readInt<unsigned int>();
    for (unsigned int samplerIndex = 0; samplerIndex < samplerCount; ++samplerIndex)
    {
        GLenum textureType  = stream.readInt<GLenum>();
        size_t bindingCount = stream.readInt<size_t>();
        mState.mSamplerBindings.emplace_back(SamplerBinding(textureType, bindingCount));
    }

    ANGLE_TRY_RESULT(mProgram->load(context->getImplementation(), mInfoLog, &stream), mLinked);

    return NoError();
#endif  // #if ANGLE_PROGRAM_BINARY_LOAD == ANGLE_ENABLED
}

Error Program::saveBinary(const Context *context,
                          GLenum *binaryFormat,
                          void *binary,
                          GLsizei bufSize,
                          GLsizei *length) const
{
    if (binaryFormat)
    {
        *binaryFormat = GL_PROGRAM_BINARY_ANGLE;
    }

    BinaryOutputStream stream;

    stream.writeBytes(reinterpret_cast<const unsigned char*>(ANGLE_COMMIT_HASH), ANGLE_COMMIT_HASH_SIZE);

    // nullptr context is supported when computing binary length.
    if (context)
    {
        stream.writeInt(context->getClientVersion().major);
        stream.writeInt(context->getClientVersion().minor);
    }
    else
    {
        stream.writeInt(2);
        stream.writeInt(0);
    }

    stream.writeInt(mState.mComputeShaderLocalSize[0]);
    stream.writeInt(mState.mComputeShaderLocalSize[1]);
    stream.writeInt(mState.mComputeShaderLocalSize[2]);

    stream.writeInt(mState.mActiveAttribLocationsMask.to_ulong());

    stream.writeInt(mState.mAttributes.size());
    for (const sh::Attribute &attrib : mState.mAttributes)
    {
        WriteShaderVar(&stream, attrib);
        stream.writeInt(attrib.location);
    }

    stream.writeInt(mState.mUniforms.size());
    for (const LinkedUniform &uniform : mState.mUniforms)
    {
        WriteShaderVar(&stream, uniform);

        // FIXME: referenced

        stream.writeInt(uniform.blockIndex);
        stream.writeInt(uniform.blockInfo.offset);
        stream.writeInt(uniform.blockInfo.arrayStride);
        stream.writeInt(uniform.blockInfo.matrixStride);
        stream.writeInt(uniform.blockInfo.isRowMajorMatrix);
    }

    stream.writeInt(mState.mUniformLocations.size());
    for (const auto &variable : mState.mUniformLocations)
    {
        stream.writeString(variable.name);
        stream.writeInt(variable.element);
        stream.writeInt(variable.index);
        stream.writeInt(variable.used);
        stream.writeInt(variable.ignored);
    }

    stream.writeInt(mState.mUniformBlocks.size());
    for (const UniformBlock &uniformBlock : mState.mUniformBlocks)
    {
        stream.writeString(uniformBlock.name);
        stream.writeInt(uniformBlock.isArray);
        stream.writeInt(uniformBlock.arrayElement);
        stream.writeInt(uniformBlock.dataSize);

        stream.writeInt(uniformBlock.vertexStaticUse);
        stream.writeInt(uniformBlock.fragmentStaticUse);

        stream.writeInt(uniformBlock.memberUniformIndexes.size());
        for (unsigned int memberUniformIndex : uniformBlock.memberUniformIndexes)
        {
            stream.writeInt(memberUniformIndex);
        }
    }

    for (GLuint binding : mState.mUniformBlockBindings)
    {
        stream.writeInt(binding);
    }

    stream.writeInt(mState.mLinkedTransformFeedbackVaryings.size());
    for (const auto &var : mState.mLinkedTransformFeedbackVaryings)
    {
        stream.writeInt(var.arraySize);
        stream.writeInt(var.type);
        stream.writeString(var.name);

        stream.writeIntOrNegOne(var.arrayIndex);
    }

    stream.writeInt(mState.mTransformFeedbackBufferMode);

    stream.writeInt(mState.mOutputVariables.size());
    for (const sh::OutputVariable &output : mState.mOutputVariables)
    {
        WriteShaderVar(&stream, output);
        stream.writeInt(output.location);
    }

    stream.writeInt(mState.mOutputLocations.size());
    for (const auto &outputPair : mState.mOutputLocations)
    {
        stream.writeInt(outputPair.first);
        stream.writeIntOrNegOne(outputPair.second.element);
        stream.writeInt(outputPair.second.index);
        stream.writeString(outputPair.second.name);
    }

    stream.writeInt(mState.mSamplerUniformRange.start);
    stream.writeInt(mState.mSamplerUniformRange.end);

    stream.writeInt(mState.mSamplerBindings.size());
    for (const auto &samplerBinding : mState.mSamplerBindings)
    {
        stream.writeInt(samplerBinding.textureType);
        stream.writeInt(samplerBinding.boundTextureUnits.size());
    }

    ANGLE_TRY(mProgram->save(&stream));

    GLsizei streamLength   = static_cast<GLsizei>(stream.length());
    const void *streamState = stream.data();

    if (streamLength > bufSize)
    {
        if (length)
        {
            *length = 0;
        }

        // TODO: This should be moved to the validation layer but computing the size of the binary before saving
        // it causes the save to happen twice.  It may be possible to write the binary to a separate buffer, validate
        // sizes and then copy it.
        return Error(GL_INVALID_OPERATION);
    }

    if (binary)
    {
        char *ptr = reinterpret_cast<char*>(binary);

        memcpy(ptr, streamState, streamLength);
        ptr += streamLength;

        ASSERT(ptr - streamLength == binary);
    }

    if (length)
    {
        *length = streamLength;
    }

    return NoError();
}

GLint Program::getBinaryLength() const
{
    GLint length;
    Error error = saveBinary(nullptr, nullptr, nullptr, std::numeric_limits<GLint>::max(), &length);
    if (error.isError())
    {
        return 0;
    }

    return length;
}

void Program::setBinaryRetrievableHint(bool retrievable)
{
    // TODO(jmadill) : replace with dirty bits
    mProgram->setBinaryRetrievableHint(retrievable);
    mState.mBinaryRetrieveableHint = retrievable;
}

bool Program::getBinaryRetrievableHint() const
{
    return mState.mBinaryRetrieveableHint;
}

void Program::setSeparable(bool separable)
{
    // TODO(yunchao) : replace with dirty bits
    if (mState.mSeparable != separable)
    {
        mProgram->setSeparable(separable);
        mState.mSeparable = separable;
    }
}

bool Program::isSeparable() const
{
    return mState.mSeparable;
}

void Program::release(const Context *context)
{
    mRefCount--;

    if (mRefCount == 0 && mDeleteStatus)
    {
        mResourceManager->deleteProgram(context, mHandle);
    }
}

void Program::addRef()
{
    mRefCount++;
}

unsigned int Program::getRefCount() const
{
    return mRefCount;
}

int Program::getInfoLogLength() const
{
    return static_cast<int>(mInfoLog.getLength());
}

void Program::getInfoLog(GLsizei bufSize, GLsizei *length, char *infoLog) const
{
    return mInfoLog.getLog(bufSize, length, infoLog);
}

void Program::getAttachedShaders(GLsizei maxCount, GLsizei *count, GLuint *shaders) const
{
    int total = 0;

    if (mState.mAttachedComputeShader)
    {
        if (total < maxCount)
        {
            shaders[total] = mState.mAttachedComputeShader->getHandle();
            total++;
        }
    }

    if (mState.mAttachedVertexShader)
    {
        if (total < maxCount)
        {
            shaders[total] = mState.mAttachedVertexShader->getHandle();
            total++;
        }
    }

    if (mState.mAttachedFragmentShader)
    {
        if (total < maxCount)
        {
            shaders[total] = mState.mAttachedFragmentShader->getHandle();
            total++;
        }
    }

    if (count)
    {
        *count = total;
    }
}

GLuint Program::getAttributeLocation(const std::string &name) const
{
    for (const sh::Attribute &attribute : mState.mAttributes)
    {
        if (attribute.name == name)
        {
            return attribute.location;
        }
    }

    return static_cast<GLuint>(-1);
}

bool Program::isAttribLocationActive(size_t attribLocation) const
{
    ASSERT(attribLocation < mState.mActiveAttribLocationsMask.size());
    return mState.mActiveAttribLocationsMask[attribLocation];
}

void Program::getActiveAttribute(GLuint index,
                                 GLsizei bufsize,
                                 GLsizei *length,
                                 GLint *size,
                                 GLenum *type,
                                 GLchar *name) const
{
    if (!mLinked)
    {
        if (bufsize > 0)
        {
            name[0] = '\0';
        }

        if (length)
        {
            *length = 0;
        }

        *type = GL_NONE;
        *size = 1;
        return;
    }

    ASSERT(index < mState.mAttributes.size());
    const sh::Attribute &attrib = mState.mAttributes[index];

    if (bufsize > 0)
    {
        CopyStringToBuffer(name, attrib.name, bufsize, length);
    }

    // Always a single 'type' instance
    *size = 1;
    *type = attrib.type;
}

GLint Program::getActiveAttributeCount() const
{
    if (!mLinked)
    {
        return 0;
    }

    return static_cast<GLint>(mState.mAttributes.size());
}

GLint Program::getActiveAttributeMaxLength() const
{
    if (!mLinked)
    {
        return 0;
    }

    size_t maxLength = 0;

    for (const sh::Attribute &attrib : mState.mAttributes)
    {
        maxLength = std::max(attrib.name.length() + 1, maxLength);
    }

    return static_cast<GLint>(maxLength);
}

GLuint Program::getInputResourceIndex(const GLchar *name) const
{
    for (GLuint attributeIndex = 0; attributeIndex < mState.mAttributes.size(); ++attributeIndex)
    {
        const sh::Attribute &attribute = mState.mAttributes[attributeIndex];
        if (attribute.name == name)
        {
            return attributeIndex;
        }
    }
    return GL_INVALID_INDEX;
}

GLuint Program::getOutputResourceIndex(const GLchar *name) const
{
    return GetResourceIndexFromName(mState.mOutputVariables, std::string(name));
}

size_t Program::getOutputResourceCount() const
{
    return (mLinked ? mState.mOutputVariables.size() : 0);
}

void Program::getInputResourceName(GLuint index,
                                   GLsizei bufSize,
                                   GLsizei *length,
                                   GLchar *name) const
{
    GLint size;
    GLenum type;
    getActiveAttribute(index, bufSize, length, &size, &type, name);
}

void Program::getOutputResourceName(GLuint index,
                                    GLsizei bufSize,
                                    GLsizei *length,
                                    GLchar *name) const
{
    if (length)
    {
        *length = 0;
    }

    if (!mLinked)
    {
        if (bufSize > 0)
        {
            name[0] = '\0';
        }
        return;
    }
    ASSERT(index < mState.mOutputVariables.size());
    const auto &output = mState.mOutputVariables[index];

    if (bufSize > 0)
    {
        std::string nameWithArray = (output.isArray() ? output.name + "[0]" : output.name);

        CopyStringToBuffer(name, nameWithArray, bufSize, length);
    }
}

GLint Program::getFragDataLocation(const std::string &name) const
{
    std::string baseName(name);
    unsigned int arrayIndex = ParseAndStripArrayIndex(&baseName);
    for (auto outputPair : mState.mOutputLocations)
    {
        const VariableLocation &outputVariable = outputPair.second;
        if (outputVariable.name == baseName && (arrayIndex == GL_INVALID_INDEX || arrayIndex == outputVariable.element))
        {
            return static_cast<GLint>(outputPair.first);
        }
    }
    return -1;
}

void Program::getActiveUniform(GLuint index,
                               GLsizei bufsize,
                               GLsizei *length,
                               GLint *size,
                               GLenum *type,
                               GLchar *name) const
{
    if (mLinked)
    {
        // index must be smaller than getActiveUniformCount()
        ASSERT(index < mState.mUniforms.size());
        const LinkedUniform &uniform = mState.mUniforms[index];

        if (bufsize > 0)
        {
            std::string string = uniform.name;
            if (uniform.isArray())
            {
                string += "[0]";
            }
            CopyStringToBuffer(name, string, bufsize, length);
        }

        *size = uniform.elementCount();
        *type = uniform.type;
    }
    else
    {
        if (bufsize > 0)
        {
            name[0] = '\0';
        }

        if (length)
        {
            *length = 0;
        }

        *size = 0;
        *type = GL_NONE;
    }
}

GLint Program::getActiveUniformCount() const
{
    if (mLinked)
    {
        return static_cast<GLint>(mState.mUniforms.size());
    }
    else
    {
        return 0;
    }
}

GLint Program::getActiveUniformMaxLength() const
{
    size_t maxLength = 0;

    if (mLinked)
    {
        for (const LinkedUniform &uniform : mState.mUniforms)
        {
            if (!uniform.name.empty())
            {
                size_t length = uniform.name.length() + 1u;
                if (uniform.isArray())
                {
                    length += 3;  // Counting in "[0]".
                }
                maxLength = std::max(length, maxLength);
            }
        }
    }

    return static_cast<GLint>(maxLength);
}

GLint Program::getActiveUniformi(GLuint index, GLenum pname) const
{
    ASSERT(static_cast<size_t>(index) < mState.mUniforms.size());
    const LinkedUniform &uniform = mState.mUniforms[index];
    switch (pname)
    {
      case GL_UNIFORM_TYPE:         return static_cast<GLint>(uniform.type);
      case GL_UNIFORM_SIZE:         return static_cast<GLint>(uniform.elementCount());
      case GL_UNIFORM_NAME_LENGTH:  return static_cast<GLint>(uniform.name.size() + 1 + (uniform.isArray() ? 3 : 0));
      case GL_UNIFORM_BLOCK_INDEX:  return uniform.blockIndex;
      case GL_UNIFORM_OFFSET:       return uniform.blockInfo.offset;
      case GL_UNIFORM_ARRAY_STRIDE: return uniform.blockInfo.arrayStride;
      case GL_UNIFORM_MATRIX_STRIDE: return uniform.blockInfo.matrixStride;
      case GL_UNIFORM_IS_ROW_MAJOR: return static_cast<GLint>(uniform.blockInfo.isRowMajorMatrix);
      default:
        UNREACHABLE();
        break;
    }
    return 0;
}

bool Program::isValidUniformLocation(GLint location) const
{
    ASSERT(angle::IsValueInRangeForNumericType<GLint>(mState.mUniformLocations.size()));
    return (location >= 0 && static_cast<size_t>(location) < mState.mUniformLocations.size() &&
            mState.mUniformLocations[static_cast<size_t>(location)].used);
}

const LinkedUniform &Program::getUniformByLocation(GLint location) const
{
    ASSERT(location >= 0 && static_cast<size_t>(location) < mState.mUniformLocations.size());
    return mState.mUniforms[mState.getUniformIndexFromLocation(location)];
}

const VariableLocation &Program::getUniformLocation(GLint location) const
{
    ASSERT(location >= 0 && static_cast<size_t>(location) < mState.mUniformLocations.size());
    return mState.mUniformLocations[location];
}

const std::vector<VariableLocation> &Program::getUniformLocations() const
{
    return mState.mUniformLocations;
}

const LinkedUniform &Program::getUniformByIndex(GLuint index) const
{
    ASSERT(index < static_cast<size_t>(mState.mUniforms.size()));
    return mState.mUniforms[index];
}

GLint Program::getUniformLocation(const std::string &name) const
{
    return mState.getUniformLocation(name);
}

GLuint Program::getUniformIndex(const std::string &name) const
{
    return mState.getUniformIndexFromName(name);
}

void Program::setUniform1fv(GLint location, GLsizei count, const GLfloat *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 1, v);
    mProgram->setUniform1fv(location, clampedCount, v);
}

void Program::setUniform2fv(GLint location, GLsizei count, const GLfloat *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 2, v);
    mProgram->setUniform2fv(location, clampedCount, v);
}

void Program::setUniform3fv(GLint location, GLsizei count, const GLfloat *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 3, v);
    mProgram->setUniform3fv(location, clampedCount, v);
}

void Program::setUniform4fv(GLint location, GLsizei count, const GLfloat *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 4, v);
    mProgram->setUniform4fv(location, clampedCount, v);
}

void Program::setUniform1iv(GLint location, GLsizei count, const GLint *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 1, v);
    mProgram->setUniform1iv(location, clampedCount, v);
}

void Program::setUniform2iv(GLint location, GLsizei count, const GLint *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 2, v);
    mProgram->setUniform2iv(location, clampedCount, v);
}

void Program::setUniform3iv(GLint location, GLsizei count, const GLint *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 3, v);
    mProgram->setUniform3iv(location, clampedCount, v);
}

void Program::setUniform4iv(GLint location, GLsizei count, const GLint *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 4, v);
    mProgram->setUniform4iv(location, clampedCount, v);
}

void Program::setUniform1uiv(GLint location, GLsizei count, const GLuint *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 1, v);
    mProgram->setUniform1uiv(location, clampedCount, v);
}

void Program::setUniform2uiv(GLint location, GLsizei count, const GLuint *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 2, v);
    mProgram->setUniform2uiv(location, clampedCount, v);
}

void Program::setUniform3uiv(GLint location, GLsizei count, const GLuint *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 3, v);
    mProgram->setUniform3uiv(location, clampedCount, v);
}

void Program::setUniform4uiv(GLint location, GLsizei count, const GLuint *v)
{
    GLsizei clampedCount = setUniformInternal(location, count, 4, v);
    mProgram->setUniform4uiv(location, clampedCount, v);
}

void Program::setUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *v)
{
    GLsizei clampedCount = setMatrixUniformInternal<2, 2>(location, count, transpose, v);
    mProgram->setUniformMatrix2fv(location, clampedCount, transpose, v);
}

void Program::setUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *v)
{
    GLsizei clampedCount = setMatrixUniformInternal<3, 3>(location, count, transpose, v);
    mProgram->setUniformMatrix3fv(location, clampedCount, transpose, v);
}

void Program::setUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *v)
{
    GLsizei clampedCount = setMatrixUniformInternal<4, 4>(location, count, transpose, v);
    mProgram->setUniformMatrix4fv(location, clampedCount, transpose, v);
}

void Program::setUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *v)
{
    GLsizei clampedCount = setMatrixUniformInternal<2, 3>(location, count, transpose, v);
    mProgram->setUniformMatrix2x3fv(location, clampedCount, transpose, v);
}

void Program::setUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *v)
{
    GLsizei clampedCount = setMatrixUniformInternal<2, 4>(location, count, transpose, v);
    mProgram->setUniformMatrix2x4fv(location, clampedCount, transpose, v);
}

void Program::setUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *v)
{
    GLsizei clampedCount = setMatrixUniformInternal<3, 2>(location, count, transpose, v);
    mProgram->setUniformMatrix3x2fv(location, clampedCount, transpose, v);
}

void Program::setUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *v)
{
    GLsizei clampedCount = setMatrixUniformInternal<3, 4>(location, count, transpose, v);
    mProgram->setUniformMatrix3x4fv(location, clampedCount, transpose, v);
}

void Program::setUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *v)
{
    GLsizei clampedCount = setMatrixUniformInternal<4, 2>(location, count, transpose, v);
    mProgram->setUniformMatrix4x2fv(location, clampedCount, transpose, v);
}

void Program::setUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *v)
{
    GLsizei clampedCount = setMatrixUniformInternal<4, 3>(location, count, transpose, v);
    mProgram->setUniformMatrix4x3fv(location, clampedCount, transpose, v);
}

void Program::getUniformfv(GLint location, GLfloat *v) const
{
    getUniformInternal(location, v);
}

void Program::getUniformiv(GLint location, GLint *v) const
{
    getUniformInternal(location, v);
}

void Program::getUniformuiv(GLint location, GLuint *v) const
{
    getUniformInternal(location, v);
}

void Program::flagForDeletion()
{
    mDeleteStatus = true;
}

bool Program::isFlaggedForDeletion() const
{
    return mDeleteStatus;
}

void Program::validate(const Caps &caps)
{
    mInfoLog.reset();

    if (mLinked)
    {
        mValidated = (mProgram->validate(caps, &mInfoLog) == GL_TRUE);
    }
    else
    {
        mInfoLog << "Program has not been successfully linked.";
    }
}

bool Program::validateSamplers(InfoLog *infoLog, const Caps &caps)
{
    // Skip cache if we're using an infolog, so we get the full error.
    // Also skip the cache if the sample mapping has changed, or if we haven't ever validated.
    if (infoLog == nullptr && mCachedValidateSamplersResult.valid())
    {
        return mCachedValidateSamplersResult.value();
    }

    if (mTextureUnitTypesCache.empty())
    {
        mTextureUnitTypesCache.resize(caps.maxCombinedTextureImageUnits, GL_NONE);
    }
    else
    {
        std::fill(mTextureUnitTypesCache.begin(), mTextureUnitTypesCache.end(), GL_NONE);
    }

    // if any two active samplers in a program are of different types, but refer to the same
    // texture image unit, and this is the current program, then ValidateProgram will fail, and
    // DrawArrays and DrawElements will issue the INVALID_OPERATION error.
    for (const auto &samplerBinding : mState.mSamplerBindings)
    {
        GLenum textureType = samplerBinding.textureType;

        for (GLuint textureUnit : samplerBinding.boundTextureUnits)
        {
            if (textureUnit >= caps.maxCombinedTextureImageUnits)
            {
                if (infoLog)
                {
                    (*infoLog) << "Sampler uniform (" << textureUnit
                               << ") exceeds GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS ("
                               << caps.maxCombinedTextureImageUnits << ")";
                }

                mCachedValidateSamplersResult = false;
                return false;
            }

            if (mTextureUnitTypesCache[textureUnit] != GL_NONE)
            {
                if (textureType != mTextureUnitTypesCache[textureUnit])
                {
                    if (infoLog)
                    {
                        (*infoLog) << "Samplers of conflicting types refer to the same texture "
                                      "image unit ("
                                   << textureUnit << ").";
                    }

                    mCachedValidateSamplersResult = false;
                    return false;
                }
            }
            else
            {
                mTextureUnitTypesCache[textureUnit] = textureType;
            }
        }
    }

    mCachedValidateSamplersResult = true;
    return true;
}

bool Program::isValidated() const
{
    return mValidated;
}

GLuint Program::getActiveUniformBlockCount() const
{
    return static_cast<GLuint>(mState.mUniformBlocks.size());
}

void Program::getActiveUniformBlockName(GLuint uniformBlockIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformBlockName) const
{
    ASSERT(
        uniformBlockIndex <
        mState.mUniformBlocks.size());  // index must be smaller than getActiveUniformBlockCount()

    const UniformBlock &uniformBlock = mState.mUniformBlocks[uniformBlockIndex];

    if (bufSize > 0)
    {
        std::string string = uniformBlock.name;

        if (uniformBlock.isArray)
        {
            string += ArrayString(uniformBlock.arrayElement);
        }
        CopyStringToBuffer(uniformBlockName, string, bufSize, length);
    }
}

GLint Program::getActiveUniformBlockMaxLength() const
{
    int maxLength = 0;

    if (mLinked)
    {
        unsigned int numUniformBlocks = static_cast<unsigned int>(mState.mUniformBlocks.size());
        for (unsigned int uniformBlockIndex = 0; uniformBlockIndex < numUniformBlocks; uniformBlockIndex++)
        {
            const UniformBlock &uniformBlock = mState.mUniformBlocks[uniformBlockIndex];
            if (!uniformBlock.name.empty())
            {
                const int length = static_cast<int>(uniformBlock.name.length()) + 1;

                // Counting in "[0]".
                const int arrayLength = (uniformBlock.isArray ? 3 : 0);

                maxLength = std::max(length + arrayLength, maxLength);
            }
        }
    }

    return maxLength;
}

GLuint Program::getUniformBlockIndex(const std::string &name) const
{
    size_t subscript     = GL_INVALID_INDEX;
    std::string baseName = ParseResourceName(name, &subscript);

    unsigned int numUniformBlocks = static_cast<unsigned int>(mState.mUniformBlocks.size());
    for (unsigned int blockIndex = 0; blockIndex < numUniformBlocks; blockIndex++)
    {
        const UniformBlock &uniformBlock = mState.mUniformBlocks[blockIndex];
        if (uniformBlock.name == baseName)
        {
            const bool arrayElementZero =
                (subscript == GL_INVALID_INDEX &&
                 (!uniformBlock.isArray || uniformBlock.arrayElement == 0));
            if (subscript == uniformBlock.arrayElement || arrayElementZero)
            {
                return blockIndex;
            }
        }
    }

    return GL_INVALID_INDEX;
}

const UniformBlock &Program::getUniformBlockByIndex(GLuint index) const
{
    ASSERT(index < static_cast<GLuint>(mState.mUniformBlocks.size()));
    return mState.mUniformBlocks[index];
}

void Program::bindUniformBlock(GLuint uniformBlockIndex, GLuint uniformBlockBinding)
{
    mState.mUniformBlockBindings[uniformBlockIndex] = uniformBlockBinding;
    mState.mActiveUniformBlockBindings.set(uniformBlockIndex, uniformBlockBinding != 0);
    mProgram->setUniformBlockBinding(uniformBlockIndex, uniformBlockBinding);
}

GLuint Program::getUniformBlockBinding(GLuint uniformBlockIndex) const
{
    return mState.getUniformBlockBinding(uniformBlockIndex);
}

void Program::resetUniformBlockBindings()
{
    for (unsigned int blockId = 0; blockId < IMPLEMENTATION_MAX_COMBINED_SHADER_UNIFORM_BUFFERS; blockId++)
    {
        mState.mUniformBlockBindings[blockId] = 0;
    }
    mState.mActiveUniformBlockBindings.reset();
}

void Program::setTransformFeedbackVaryings(GLsizei count, const GLchar *const *varyings, GLenum bufferMode)
{
    mState.mTransformFeedbackVaryingNames.resize(count);
    for (GLsizei i = 0; i < count; i++)
    {
        mState.mTransformFeedbackVaryingNames[i] = varyings[i];
    }

    mState.mTransformFeedbackBufferMode = bufferMode;
}

void Program::getTransformFeedbackVarying(GLuint index, GLsizei bufSize, GLsizei *length, GLsizei *size, GLenum *type, GLchar *name) const
{
    if (mLinked)
    {
        ASSERT(index < mState.mLinkedTransformFeedbackVaryings.size());
        const auto &var     = mState.mLinkedTransformFeedbackVaryings[index];
        std::string varName = var.nameWithArrayIndex();
        GLsizei lastNameIdx = std::min(bufSize - 1, static_cast<GLsizei>(varName.length()));
        if (length)
        {
            *length = lastNameIdx;
        }
        if (size)
        {
            *size = var.size();
        }
        if (type)
        {
            *type = var.type;
        }
        if (name)
        {
            memcpy(name, varName.c_str(), lastNameIdx);
            name[lastNameIdx] = '\0';
        }
    }
}

GLsizei Program::getTransformFeedbackVaryingCount() const
{
    if (mLinked)
    {
        return static_cast<GLsizei>(mState.mLinkedTransformFeedbackVaryings.size());
    }
    else
    {
        return 0;
    }
}

GLsizei Program::getTransformFeedbackVaryingMaxLength() const
{
    if (mLinked)
    {
        GLsizei maxSize = 0;
        for (const auto &var : mState.mLinkedTransformFeedbackVaryings)
        {
            maxSize =
                std::max(maxSize, static_cast<GLsizei>(var.nameWithArrayIndex().length() + 1));
        }

        return maxSize;
    }
    else
    {
        return 0;
    }
}

GLenum Program::getTransformFeedbackBufferMode() const
{
    return mState.mTransformFeedbackBufferMode;
}

bool Program::linkVaryings(InfoLog &infoLog) const
{
    const Shader *vertexShader   = mState.mAttachedVertexShader;
    const Shader *fragmentShader = mState.mAttachedFragmentShader;

    ASSERT(vertexShader->getShaderVersion() == fragmentShader->getShaderVersion());

    const std::vector<sh::Varying> &vertexVaryings   = vertexShader->getVaryings();
    const std::vector<sh::Varying> &fragmentVaryings = fragmentShader->getVaryings();

    std::map<GLuint, std::string> staticFragmentInputLocations;

    for (const sh::Varying &output : fragmentVaryings)
    {
        bool matched = false;

        // Built-in varyings obey special rules
        if (output.isBuiltIn())
        {
            continue;
        }

        for (const sh::Varying &input : vertexVaryings)
        {
            if (output.name == input.name)
            {
                ASSERT(!input.isBuiltIn());
                if (!linkValidateVaryings(infoLog, output.name, input, output,
                                          vertexShader->getShaderVersion()))
                {
                    return false;
                }

                matched = true;
                break;
            }
        }

        // We permit unmatched, unreferenced varyings
        if (!matched && output.staticUse)
        {
            infoLog << "Fragment varying " << output.name << " does not match any vertex varying";
            return false;
        }

        // Check for aliased path rendering input bindings (if any).
        // If more than one binding refer statically to the same
        // location the link must fail.

        if (!output.staticUse)
            continue;

        const auto inputBinding = mFragmentInputBindings.getBinding(output.name);
        if (inputBinding == -1)
            continue;

        const auto it = staticFragmentInputLocations.find(inputBinding);
        if (it == std::end(staticFragmentInputLocations))
        {
            staticFragmentInputLocations.insert(std::make_pair(inputBinding, output.name));
        }
        else
        {
            infoLog << "Binding for fragment input " << output.name << " conflicts with "
                    << it->second;
            return false;
        }
    }

    if (!linkValidateBuiltInVaryings(infoLog))
    {
        return false;
    }

    // TODO(jmadill): verify no unmatched vertex varyings?

    return true;
}

bool Program::linkUniforms(InfoLog &infoLog,
                           const Caps &caps,
                           const Bindings &uniformLocationBindings)
{
    UniformLinker linker(mState);
    if (!linker.link(infoLog, caps, uniformLocationBindings))
    {
        return false;
    }

    linker.getResults(&mState.mUniforms, &mState.mUniformLocations);

    linkSamplerBindings();

    return true;
}

void Program::linkSamplerBindings()
{
    mState.mSamplerUniformRange.end   = static_cast<unsigned int>(mState.mUniforms.size());
    mState.mSamplerUniformRange.start = mState.mSamplerUniformRange.end;
    auto samplerIter                  = mState.mUniforms.rbegin();
    while (samplerIter != mState.mUniforms.rend() && samplerIter->isSampler())
    {
        --mState.mSamplerUniformRange.start;
        ++samplerIter;
    }
    // If uniform is a sampler type, insert it into the mSamplerBindings array.
    for (unsigned int samplerIndex = mState.mSamplerUniformRange.start;
         samplerIndex < mState.mUniforms.size(); ++samplerIndex)
    {
        const auto &samplerUniform = mState.mUniforms[samplerIndex];
        GLenum textureType         = SamplerTypeToTextureType(samplerUniform.type);
        mState.mSamplerBindings.emplace_back(
            SamplerBinding(textureType, samplerUniform.elementCount()));
    }
}

bool Program::linkValidateInterfaceBlockFields(InfoLog &infoLog,
                                               const std::string &uniformName,
                                               const sh::InterfaceBlockField &vertexUniform,
                                               const sh::InterfaceBlockField &fragmentUniform)
{
    // We don't validate precision on UBO fields. See resolution of Khronos bug 10287.
    if (!linkValidateVariablesBase(infoLog, uniformName, vertexUniform, fragmentUniform, false))
    {
        return false;
    }

    if (vertexUniform.isRowMajorLayout != fragmentUniform.isRowMajorLayout)
    {
        infoLog << "Matrix packings for " << uniformName << " differ between vertex and fragment shaders";
        return false;
    }

    return true;
}

// Assigns locations to all attributes from the bindings and program locations.
bool Program::linkAttributes(const ContextState &data, InfoLog &infoLog)
{
    const auto *vertexShader = mState.getAttachedVertexShader();

    unsigned int usedLocations = 0;
    mState.mAttributes         = vertexShader->getActiveAttributes();
    GLuint maxAttribs          = data.getCaps().maxVertexAttributes;

    // TODO(jmadill): handle aliasing robustly
    if (mState.mAttributes.size() > maxAttribs)
    {
        infoLog << "Too many vertex attributes.";
        return false;
    }

    std::vector<sh::Attribute *> usedAttribMap(maxAttribs, nullptr);

    // Link attributes that have a binding location
    for (sh::Attribute &attribute : mState.mAttributes)
    {
        int bindingLocation = mAttributeBindings.getBinding(attribute.name);
        if (attribute.location == -1 && bindingLocation != -1)
        {
            attribute.location = bindingLocation;
        }

        if (attribute.location != -1)
        {
            // Location is set by glBindAttribLocation or by location layout qualifier
            const int regs = VariableRegisterCount(attribute.type);

            if (static_cast<GLuint>(regs + attribute.location) > maxAttribs)
            {
                infoLog << "Active attribute (" << attribute.name << ") at location "
                        << attribute.location << " is too big to fit";

                return false;
            }

            for (int reg = 0; reg < regs; reg++)
            {
                const int regLocation               = attribute.location + reg;
                sh::ShaderVariable *linkedAttribute = usedAttribMap[regLocation];

                // In GLSL 3.00, attribute aliasing produces a link error
                // In GLSL 1.00, attribute aliasing is allowed, but ANGLE currently has a bug
                if (linkedAttribute)
                {
                    // TODO(jmadill): fix aliasing on ES2
                    // if (mProgram->getShaderVersion() >= 300)
                    {
                        infoLog << "Attribute '" << attribute.name << "' aliases attribute '"
                                << linkedAttribute->name << "' at location " << regLocation;
                        return false;
                    }
                }
                else
                {
                    usedAttribMap[regLocation] = &attribute;
                }

                usedLocations |= 1 << regLocation;
            }
        }
    }

    // Link attributes that don't have a binding location
    for (sh::Attribute &attribute : mState.mAttributes)
    {
        // Not set by glBindAttribLocation or by location layout qualifier
        if (attribute.location == -1)
        {
            int regs           = VariableRegisterCount(attribute.type);
            int availableIndex = AllocateFirstFreeBits(&usedLocations, regs, maxAttribs);

            if (availableIndex == -1 || static_cast<GLuint>(availableIndex + regs) > maxAttribs)
            {
                infoLog << "Too many active attributes (" << attribute.name << ")";
                return false;
            }

            attribute.location = availableIndex;
        }
    }

    for (const sh::Attribute &attribute : mState.mAttributes)
    {
        ASSERT(attribute.location != -1);
        int regs = VariableRegisterCount(attribute.type);

        for (int r = 0; r < regs; r++)
        {
            mState.mActiveAttribLocationsMask.set(attribute.location + r);
        }
    }

    return true;
}

bool Program::validateUniformBlocksCount(GLuint maxUniformBlocks,
                                         const std::vector<sh::InterfaceBlock> &intefaceBlocks,
                                         const std::string &errorMessage,
                                         InfoLog &infoLog) const
{
    GLuint blockCount = 0;
    for (const sh::InterfaceBlock &block : intefaceBlocks)
    {
        if (block.staticUse || block.layout != sh::BLOCKLAYOUT_PACKED)
        {
            if (++blockCount > maxUniformBlocks)
            {
                infoLog << errorMessage << maxUniformBlocks << ")";
                return false;
            }
        }
    }
    return true;
}

bool Program::validateVertexAndFragmentInterfaceBlocks(
    const std::vector<sh::InterfaceBlock> &vertexInterfaceBlocks,
    const std::vector<sh::InterfaceBlock> &fragmentInterfaceBlocks,
    InfoLog &infoLog) const
{
    // Check that interface blocks defined in the vertex and fragment shaders are identical
    typedef std::map<std::string, const sh::InterfaceBlock *> UniformBlockMap;
    UniformBlockMap linkedUniformBlocks;

    for (const sh::InterfaceBlock &vertexInterfaceBlock : vertexInterfaceBlocks)
    {
        linkedUniformBlocks[vertexInterfaceBlock.name] = &vertexInterfaceBlock;
    }

    for (const sh::InterfaceBlock &fragmentInterfaceBlock : fragmentInterfaceBlocks)
    {
        auto entry = linkedUniformBlocks.find(fragmentInterfaceBlock.name);
        if (entry != linkedUniformBlocks.end())
        {
            const sh::InterfaceBlock &vertexInterfaceBlock = *entry->second;
            if (!areMatchingInterfaceBlocks(infoLog, vertexInterfaceBlock, fragmentInterfaceBlock))
            {
                return false;
            }
        }
    }
    return true;
}

bool Program::linkUniformBlocks(InfoLog &infoLog, const Caps &caps)
{
    if (mState.mAttachedComputeShader)
    {
        const Shader &computeShader        = *mState.mAttachedComputeShader;
        const auto &computeInterfaceBlocks = computeShader.getInterfaceBlocks();

        if (!validateUniformBlocksCount(
                caps.maxComputeUniformBlocks, computeInterfaceBlocks,
                "Compute shader uniform block count exceeds GL_MAX_COMPUTE_UNIFORM_BLOCKS (",
                infoLog))
        {
            return false;
        }
        return true;
    }

    const Shader &vertexShader   = *mState.mAttachedVertexShader;
    const Shader &fragmentShader = *mState.mAttachedFragmentShader;

    const auto &vertexInterfaceBlocks   = vertexShader.getInterfaceBlocks();
    const auto &fragmentInterfaceBlocks = fragmentShader.getInterfaceBlocks();

    if (!validateUniformBlocksCount(
            caps.maxVertexUniformBlocks, vertexInterfaceBlocks,
            "Vertex shader uniform block count exceeds GL_MAX_VERTEX_UNIFORM_BLOCKS (", infoLog))
    {
        return false;
    }
    if (!validateUniformBlocksCount(
            caps.maxFragmentUniformBlocks, fragmentInterfaceBlocks,
            "Fragment shader uniform block count exceeds GL_MAX_FRAGMENT_UNIFORM_BLOCKS (",
            infoLog))
    {

        return false;
    }
    if (!validateVertexAndFragmentInterfaceBlocks(vertexInterfaceBlocks, fragmentInterfaceBlocks,
                                                  infoLog))
    {
        return false;
    }

    return true;
}

bool Program::areMatchingInterfaceBlocks(InfoLog &infoLog,
                                         const sh::InterfaceBlock &vertexInterfaceBlock,
                                         const sh::InterfaceBlock &fragmentInterfaceBlock) const
{
    const char* blockName = vertexInterfaceBlock.name.c_str();
    // validate blocks for the same member types
    if (vertexInterfaceBlock.fields.size() != fragmentInterfaceBlock.fields.size())
    {
        infoLog << "Types for interface block '" << blockName
                << "' differ between vertex and fragment shaders";
        return false;
    }
    if (vertexInterfaceBlock.arraySize != fragmentInterfaceBlock.arraySize)
    {
        infoLog << "Array sizes differ for interface block '" << blockName
                << "' between vertex and fragment shaders";
        return false;
    }
    if (vertexInterfaceBlock.layout != fragmentInterfaceBlock.layout || vertexInterfaceBlock.isRowMajorLayout != fragmentInterfaceBlock.isRowMajorLayout)
    {
        infoLog << "Layout qualifiers differ for interface block '" << blockName
                << "' between vertex and fragment shaders";
        return false;
    }
    const unsigned int numBlockMembers =
        static_cast<unsigned int>(vertexInterfaceBlock.fields.size());
    for (unsigned int blockMemberIndex = 0; blockMemberIndex < numBlockMembers; blockMemberIndex++)
    {
        const sh::InterfaceBlockField &vertexMember = vertexInterfaceBlock.fields[blockMemberIndex];
        const sh::InterfaceBlockField &fragmentMember = fragmentInterfaceBlock.fields[blockMemberIndex];
        if (vertexMember.name != fragmentMember.name)
        {
            infoLog << "Name mismatch for field " << blockMemberIndex
                    << " of interface block '" << blockName
                    << "': (in vertex: '" << vertexMember.name
                    << "', in fragment: '" << fragmentMember.name << "')";
            return false;
        }
        std::string memberName = "interface block '" + vertexInterfaceBlock.name + "' member '" + vertexMember.name + "'";
        if (!linkValidateInterfaceBlockFields(infoLog, memberName, vertexMember, fragmentMember))
        {
            return false;
        }
    }
    return true;
}

bool Program::linkValidateVariablesBase(InfoLog &infoLog, const std::string &variableName, const sh::ShaderVariable &vertexVariable,
                                              const sh::ShaderVariable &fragmentVariable, bool validatePrecision)
{
    if (vertexVariable.type != fragmentVariable.type)
    {
        infoLog << "Types for " << variableName << " differ between vertex and fragment shaders";
        return false;
    }
    if (vertexVariable.arraySize != fragmentVariable.arraySize)
    {
        infoLog << "Array sizes for " << variableName << " differ between vertex and fragment shaders";
        return false;
    }
    if (validatePrecision && vertexVariable.precision != fragmentVariable.precision)
    {
        infoLog << "Precisions for " << variableName << " differ between vertex and fragment shaders";
        return false;
    }

    if (vertexVariable.fields.size() != fragmentVariable.fields.size())
    {
        infoLog << "Structure lengths for " << variableName << " differ between vertex and fragment shaders";
        return false;
    }
    const unsigned int numMembers = static_cast<unsigned int>(vertexVariable.fields.size());
    for (unsigned int memberIndex = 0; memberIndex < numMembers; memberIndex++)
    {
        const sh::ShaderVariable &vertexMember = vertexVariable.fields[memberIndex];
        const sh::ShaderVariable &fragmentMember = fragmentVariable.fields[memberIndex];

        if (vertexMember.name != fragmentMember.name)
        {
            infoLog << "Name mismatch for field '" << memberIndex
                    << "' of " << variableName
                    << ": (in vertex: '" << vertexMember.name
                    << "', in fragment: '" << fragmentMember.name << "')";
            return false;
        }

        const std::string memberName = variableName.substr(0, variableName.length() - 1) + "." +
                                       vertexMember.name + "'";

        if (!linkValidateVariablesBase(infoLog, vertexMember.name, vertexMember, fragmentMember, validatePrecision))
        {
            return false;
        }
    }

    return true;
}

bool Program::linkValidateVaryings(InfoLog &infoLog,
                                   const std::string &varyingName,
                                   const sh::Varying &vertexVarying,
                                   const sh::Varying &fragmentVarying,
                                   int shaderVersion)
{
    if (!linkValidateVariablesBase(infoLog, varyingName, vertexVarying, fragmentVarying, false))
    {
        return false;
    }

    if (!sh::InterpolationTypesMatch(vertexVarying.interpolation, fragmentVarying.interpolation))
    {
        infoLog << "Interpolation types for " << varyingName
                << " differ between vertex and fragment shaders.";
        return false;
    }

    if (shaderVersion == 100 && vertexVarying.isInvariant != fragmentVarying.isInvariant)
    {
        infoLog << "Invariance for " << varyingName
                << " differs between vertex and fragment shaders.";
        return false;
    }

    return true;
}

bool Program::linkValidateBuiltInVaryings(InfoLog &infoLog) const
{
    const Shader *vertexShader                       = mState.mAttachedVertexShader;
    const Shader *fragmentShader                     = mState.mAttachedFragmentShader;
    const std::vector<sh::Varying> &vertexVaryings   = vertexShader->getVaryings();
    const std::vector<sh::Varying> &fragmentVaryings = fragmentShader->getVaryings();
    int shaderVersion                                = vertexShader->getShaderVersion();

    if (shaderVersion != 100)
    {
        // Only ESSL 1.0 has restrictions on matching input and output invariance
        return true;
    }

    bool glPositionIsInvariant   = false;
    bool glPointSizeIsInvariant  = false;
    bool glFragCoordIsInvariant  = false;
    bool glPointCoordIsInvariant = false;

    for (const sh::Varying &varying : vertexVaryings)
    {
        if (!varying.isBuiltIn())
        {
            continue;
        }
        if (varying.name.compare("gl_Position") == 0)
        {
            glPositionIsInvariant = varying.isInvariant;
        }
        else if (varying.name.compare("gl_PointSize") == 0)
        {
            glPointSizeIsInvariant = varying.isInvariant;
        }
    }

    for (const sh::Varying &varying : fragmentVaryings)
    {
        if (!varying.isBuiltIn())
        {
            continue;
        }
        if (varying.name.compare("gl_FragCoord") == 0)
        {
            glFragCoordIsInvariant = varying.isInvariant;
        }
        else if (varying.name.compare("gl_PointCoord") == 0)
        {
            glPointCoordIsInvariant = varying.isInvariant;
        }
    }

    // There is some ambiguity in ESSL 1.00.17 paragraph 4.6.4 interpretation,
    // for example, https://cvs.khronos.org/bugzilla/show_bug.cgi?id=13842.
    // Not requiring invariance to match is supported by:
    // dEQP, WebGL CTS, Nexus 5X GLES
    if (glFragCoordIsInvariant && !glPositionIsInvariant)
    {
        infoLog << "gl_FragCoord can only be declared invariant if and only if gl_Position is "
                   "declared invariant.";
        return false;
    }
    if (glPointCoordIsInvariant && !glPointSizeIsInvariant)
    {
        infoLog << "gl_PointCoord can only be declared invariant if and only if gl_PointSize is "
                   "declared invariant.";
        return false;
    }

    return true;
}

bool Program::linkValidateTransformFeedback(const gl::Context *context,
                                            InfoLog &infoLog,
                                            const Program::MergedVaryings &varyings,
                                            const Caps &caps) const
{
    size_t totalComponents = 0;

    std::set<std::string> uniqueNames;

    for (const std::string &tfVaryingName : mState.mTransformFeedbackVaryingNames)
    {
        bool found = false;
        size_t subscript     = GL_INVALID_INDEX;
        std::string baseName = ParseResourceName(tfVaryingName, &subscript);

        for (const auto &ref : varyings)
        {
            const sh::Varying *varying = ref.second.get();

            if (baseName == varying->name)
            {
                if (uniqueNames.count(tfVaryingName) > 0)
                {
                    infoLog << "Two transform feedback varyings specify the same output variable ("
                            << tfVaryingName << ").";
                    return false;
                }
                if (context->getClientVersion() >= Version(3, 1))
                {
                    if (IncludeSameArrayElement(uniqueNames, tfVaryingName))
                    {
                        infoLog
                            << "Two transform feedback varyings include the same array element ("
                            << tfVaryingName << ").";
                        return false;
                    }
                }
                else if (varying->isArray())
                {
                    infoLog << "Capture of arrays is undefined and not supported.";
                    return false;
                }

                uniqueNames.insert(tfVaryingName);

                // TODO(jmadill): Investigate implementation limits on D3D11
                size_t elementCount =
                    ((varying->isArray() && subscript == GL_INVALID_INDEX) ? varying->elementCount()
                                                                           : 1);
                size_t componentCount = VariableComponentCount(varying->type) * elementCount;
                if (mState.mTransformFeedbackBufferMode == GL_SEPARATE_ATTRIBS &&
                    componentCount > caps.maxTransformFeedbackSeparateComponents)
                {
                    infoLog << "Transform feedback varying's " << varying->name << " components ("
                            << componentCount << ") exceed the maximum separate components ("
                            << caps.maxTransformFeedbackSeparateComponents << ").";
                    return false;
                }

                totalComponents += componentCount;
                found = true;
                break;
            }
        }
        if (context->getClientVersion() < Version(3, 1) &&
            tfVaryingName.find('[') != std::string::npos)
        {
            infoLog << "Capture of array elements is undefined and not supported.";
            return false;
        }
        // All transform feedback varyings are expected to exist since packVaryings checks for them.
        ASSERT(found);
    }

    if (mState.mTransformFeedbackBufferMode == GL_INTERLEAVED_ATTRIBS &&
        totalComponents > caps.maxTransformFeedbackInterleavedComponents)
    {
        infoLog << "Transform feedback varying total components (" << totalComponents
                << ") exceed the maximum interleaved components ("
                << caps.maxTransformFeedbackInterleavedComponents << ").";
        return false;
    }

    return true;
}

void Program::gatherTransformFeedbackVaryings(const Program::MergedVaryings &varyings)
{
    // Gather the linked varyings that are used for transform feedback, they should all exist.
    mState.mLinkedTransformFeedbackVaryings.clear();
    for (const std::string &tfVaryingName : mState.mTransformFeedbackVaryingNames)
    {
        size_t subscript     = GL_INVALID_INDEX;
        std::string baseName = ParseResourceName(tfVaryingName, &subscript);
        for (const auto &ref : varyings)
        {
            const sh::Varying *varying = ref.second.get();
            if (baseName == varying->name)
            {
                mState.mLinkedTransformFeedbackVaryings.emplace_back(
                    *varying, static_cast<GLuint>(subscript));
                break;
            }
        }
    }
}

Program::MergedVaryings Program::getMergedVaryings() const
{
    MergedVaryings merged;

    for (const sh::Varying &varying : mState.mAttachedVertexShader->getVaryings())
    {
        merged[varying.name].vertex = &varying;
    }

    for (const sh::Varying &varying : mState.mAttachedFragmentShader->getVaryings())
    {
        merged[varying.name].fragment = &varying;
    }

    return merged;
}

std::vector<PackedVarying> Program::getPackedVaryings(
    const Program::MergedVaryings &mergedVaryings) const
{
    const std::vector<std::string> &tfVaryings = mState.getTransformFeedbackVaryingNames();
    std::vector<PackedVarying> packedVaryings;
    std::set<std::string> uniqueFullNames;

    for (const auto &ref : mergedVaryings)
    {
        const sh::Varying *input  = ref.second.vertex;
        const sh::Varying *output = ref.second.fragment;

        // Only pack varyings that have a matched input or output, plus special builtins.
        if ((input && output) || (output && output->isBuiltIn()))
        {
            // Will get the vertex shader interpolation by default.
            auto interpolation = ref.second.get()->interpolation;

            // Interpolation qualifiers must match.
            if (output->isStruct())
            {
                ASSERT(!output->isArray());
                for (const auto &field : output->fields)
                {
                    ASSERT(!field.isStruct() && !field.isArray());
                    packedVaryings.push_back(PackedVarying(field, interpolation, output->name));
                }
            }
            else
            {
                packedVaryings.push_back(PackedVarying(*output, interpolation));
            }
            continue;
        }

        // Keep Transform FB varyings in the merged list always.
        if (!input)
        {
            continue;
        }

        for (const std::string &tfVarying : tfVaryings)
        {
            size_t subscript     = GL_INVALID_INDEX;
            std::string baseName = ParseResourceName(tfVarying, &subscript);
            if (uniqueFullNames.count(tfVarying) > 0)
            {
                continue;
            }
            if (baseName == input->name)
            {
                // Transform feedback for varying structs is underspecified.
                // See Khronos bug 9856.
                // TODO(jmadill): Figure out how to be spec-compliant here.
                if (!input->isStruct())
                {
                    packedVaryings.push_back(PackedVarying(*input, input->interpolation));
                    packedVaryings.back().vertexOnly = true;
                    packedVaryings.back().arrayIndex = static_cast<GLuint>(subscript);
                    uniqueFullNames.insert(tfVarying);
                }
                if (subscript == GL_INVALID_INDEX)
                {
                    break;
                }
            }
        }
    }

    std::sort(packedVaryings.begin(), packedVaryings.end(), ComparePackedVarying);

    return packedVaryings;
}

void Program::linkOutputVariables()
{
    const Shader *fragmentShader = mState.mAttachedFragmentShader;
    ASSERT(fragmentShader != nullptr);

    // Skip this step for GLES2 shaders.
    if (fragmentShader->getShaderVersion() == 100)
        return;

    mState.mOutputVariables = fragmentShader->getActiveOutputVariables();
    // TODO(jmadill): any caps validation here?

    for (unsigned int outputVariableIndex = 0; outputVariableIndex < mState.mOutputVariables.size();
         outputVariableIndex++)
    {
        const sh::OutputVariable &outputVariable = mState.mOutputVariables[outputVariableIndex];

        // Don't store outputs for gl_FragDepth, gl_FragColor, etc.
        if (outputVariable.isBuiltIn())
            continue;

        // Since multiple output locations must be specified, use 0 for non-specified locations.
        int baseLocation = (outputVariable.location == -1 ? 0 : outputVariable.location);

        for (unsigned int elementIndex = 0; elementIndex < outputVariable.elementCount();
             elementIndex++)
        {
            const int location = baseLocation + elementIndex;
            ASSERT(mState.mOutputLocations.count(location) == 0);
            unsigned int element = outputVariable.isArray() ? elementIndex : GL_INVALID_INDEX;
            mState.mOutputLocations[location] =
                VariableLocation(outputVariable.name, element, outputVariableIndex);
        }
    }
}

void Program::setUniformValuesFromBindingQualifiers()
{
    for (unsigned int samplerIndex = mState.mSamplerUniformRange.start;
         samplerIndex < mState.mSamplerUniformRange.end; ++samplerIndex)
    {
        const auto &samplerUniform = mState.mUniforms[samplerIndex];
        if (samplerUniform.binding != -1)
        {
            GLint location = mState.getUniformLocation(samplerUniform.name);
            ASSERT(location != -1);
            std::vector<GLint> boundTextureUnits;
            for (unsigned int elementIndex = 0; elementIndex < samplerUniform.elementCount();
                 ++elementIndex)
            {
                boundTextureUnits.push_back(samplerUniform.binding + elementIndex);
            }
            setUniform1iv(location, static_cast<GLsizei>(boundTextureUnits.size()),
                          boundTextureUnits.data());
        }
    }
}

void Program::gatherInterfaceBlockInfo()
{
    ASSERT(mState.mUniformBlocks.empty());

    if (mState.mAttachedComputeShader)
    {
        const Shader *computeShader = mState.getAttachedComputeShader();

        for (const sh::InterfaceBlock &computeBlock : computeShader->getInterfaceBlocks())
        {

            // Only 'packed' blocks are allowed to be considered inactive.
            if (!computeBlock.staticUse && computeBlock.layout == sh::BLOCKLAYOUT_PACKED)
                continue;

            for (UniformBlock &block : mState.mUniformBlocks)
            {
                if (block.name == computeBlock.name)
                {
                    block.computeStaticUse = computeBlock.staticUse;
                }
            }

            defineUniformBlock(computeBlock, GL_COMPUTE_SHADER);
        }
        return;
    }

    std::set<std::string> visitedList;

    const Shader *vertexShader = mState.getAttachedVertexShader();

    for (const sh::InterfaceBlock &vertexBlock : vertexShader->getInterfaceBlocks())
    {
        // Only 'packed' blocks are allowed to be considered inactive.
        if (!vertexBlock.staticUse && vertexBlock.layout == sh::BLOCKLAYOUT_PACKED)
            continue;

        if (visitedList.count(vertexBlock.name) > 0)
            continue;

        defineUniformBlock(vertexBlock, GL_VERTEX_SHADER);
        visitedList.insert(vertexBlock.name);
    }

    const Shader *fragmentShader = mState.getAttachedFragmentShader();

    for (const sh::InterfaceBlock &fragmentBlock : fragmentShader->getInterfaceBlocks())
    {
        // Only 'packed' blocks are allowed to be considered inactive.
        if (!fragmentBlock.staticUse && fragmentBlock.layout == sh::BLOCKLAYOUT_PACKED)
            continue;

        if (visitedList.count(fragmentBlock.name) > 0)
        {
            for (UniformBlock &block : mState.mUniformBlocks)
            {
                if (block.name == fragmentBlock.name)
                {
                    block.fragmentStaticUse = fragmentBlock.staticUse;
                }
            }

            continue;
        }

        defineUniformBlock(fragmentBlock, GL_FRAGMENT_SHADER);
        visitedList.insert(fragmentBlock.name);
    }
}

template <typename VarT>
void Program::defineUniformBlockMembers(const std::vector<VarT> &fields,
                                        const std::string &prefix,
                                        int blockIndex)
{
    for (const VarT &field : fields)
    {
        const std::string &fullName = (prefix.empty() ? field.name : prefix + "." + field.name);

        if (field.isStruct())
        {
            for (unsigned int arrayElement = 0; arrayElement < field.elementCount(); arrayElement++)
            {
                const std::string uniformElementName =
                    fullName + (field.isArray() ? ArrayString(arrayElement) : "");
                defineUniformBlockMembers(field.fields, uniformElementName, blockIndex);
            }
        }
        else
        {
            // If getBlockMemberInfo returns false, the uniform is optimized out.
            sh::BlockMemberInfo memberInfo;
            if (!mProgram->getUniformBlockMemberInfo(fullName, &memberInfo))
            {
                continue;
            }

            LinkedUniform newUniform(field.type, field.precision, fullName, field.arraySize, -1, -1,
                                     blockIndex, memberInfo);

            // Since block uniforms have no location, we don't need to store them in the uniform
            // locations list.
            mState.mUniforms.push_back(newUniform);
        }
    }
}

void Program::defineUniformBlock(const sh::InterfaceBlock &interfaceBlock, GLenum shaderType)
{
    int blockIndex   = static_cast<int>(mState.mUniformBlocks.size());
    size_t blockSize = 0;

    // Don't define this block at all if it's not active in the implementation.
    std::stringstream blockNameStr;
    blockNameStr << interfaceBlock.name;
    if (interfaceBlock.arraySize > 0)
    {
        blockNameStr << "[0]";
    }
    if (!mProgram->getUniformBlockSize(blockNameStr.str(), &blockSize))
    {
        return;
    }

    // Track the first and last uniform index to determine the range of active uniforms in the
    // block.
    size_t firstBlockUniformIndex = mState.mUniforms.size();
    defineUniformBlockMembers(interfaceBlock.fields, interfaceBlock.fieldPrefix(), blockIndex);
    size_t lastBlockUniformIndex = mState.mUniforms.size();

    std::vector<unsigned int> blockUniformIndexes;
    for (size_t blockUniformIndex = firstBlockUniformIndex;
         blockUniformIndex < lastBlockUniformIndex; ++blockUniformIndex)
    {
        blockUniformIndexes.push_back(static_cast<unsigned int>(blockUniformIndex));
    }

    if (interfaceBlock.arraySize > 0)
    {
        for (unsigned int arrayElement = 0; arrayElement < interfaceBlock.arraySize; ++arrayElement)
        {
            UniformBlock block(interfaceBlock.name, true, arrayElement);
            block.memberUniformIndexes = blockUniformIndexes;

            switch (shaderType)
            {
                case GL_VERTEX_SHADER:
                {
                    block.vertexStaticUse = interfaceBlock.staticUse;
                    break;
                }
                case GL_FRAGMENT_SHADER:
                {
                    block.fragmentStaticUse = interfaceBlock.staticUse;
                    break;
                }
                case GL_COMPUTE_SHADER:
                {
                    block.computeStaticUse = interfaceBlock.staticUse;
                    break;
                }
                default:
                    UNREACHABLE();
            }

            // Since all block elements in an array share the same active uniforms, they will all be
            // active once any uniform member is used. So, since interfaceBlock.name[0] was active,
            // here we will add every block element in the array.
            block.dataSize = static_cast<unsigned int>(blockSize);
            mState.mUniformBlocks.push_back(block);
        }
    }
    else
    {
        UniformBlock block(interfaceBlock.name, false, 0);
        block.memberUniformIndexes = blockUniformIndexes;

        switch (shaderType)
        {
            case GL_VERTEX_SHADER:
            {
                block.vertexStaticUse = interfaceBlock.staticUse;
                break;
            }
            case GL_FRAGMENT_SHADER:
            {
                block.fragmentStaticUse = interfaceBlock.staticUse;
                break;
            }
            case GL_COMPUTE_SHADER:
            {
                block.computeStaticUse = interfaceBlock.staticUse;
                break;
            }
            default:
                UNREACHABLE();
        }

        block.dataSize = static_cast<unsigned int>(blockSize);
        mState.mUniformBlocks.push_back(block);
    }
}

template <>
void Program::updateSamplerUniform(const VariableLocation &locationInfo,
                                   const uint8_t *destPointer,
                                   GLsizei clampedCount,
                                   const GLint *v)
{
    // Invalidate the validation cache only if we modify the sampler data.
    if (mState.isSamplerUniformIndex(locationInfo.index) &&
        memcmp(destPointer, v, sizeof(GLint) * clampedCount) != 0)
    {
        GLuint samplerIndex = mState.getSamplerIndexFromUniformIndex(locationInfo.index);
        std::vector<GLuint> *boundTextureUnits =
            &mState.mSamplerBindings[samplerIndex].boundTextureUnits;

        std::copy(v, v + clampedCount, boundTextureUnits->begin() + locationInfo.element);
        mCachedValidateSamplersResult.reset();
    }
}

template <typename T>
void Program::updateSamplerUniform(const VariableLocation &locationInfo,
                                   const uint8_t *destPointer,
                                   GLsizei clampedCount,
                                   const T *v)
{
}

template <typename T>
GLsizei Program::setUniformInternal(GLint location, GLsizei countIn, int vectorSize, const T *v)
{
    const VariableLocation &locationInfo = mState.mUniformLocations[location];
    LinkedUniform *linkedUniform         = &mState.mUniforms[locationInfo.index];
    uint8_t *destPointer                 = linkedUniform->getDataPtrToElement(locationInfo.element);

    // OpenGL ES 3.0.4 spec pg 67: "Values for any array element that exceeds the highest array
    // element index used, as reported by GetActiveUniform, will be ignored by the GL."
    unsigned int remainingElements = linkedUniform->elementCount() - locationInfo.element;
    GLsizei maxElementCount =
        static_cast<GLsizei>(remainingElements * linkedUniform->getElementComponents());

    GLsizei count        = countIn;
    GLsizei clampedCount = count * vectorSize;
    if (clampedCount > maxElementCount)
    {
        clampedCount = maxElementCount;
        count        = maxElementCount / vectorSize;
    }

    if (VariableComponentType(linkedUniform->type) == GL_BOOL)
    {
        // Do a cast conversion for boolean types. From the spec:
        // "The uniform is set to FALSE if the input value is 0 or 0.0f, and set to TRUE otherwise."
        GLint *destAsInt = reinterpret_cast<GLint *>(destPointer);
        for (GLsizei component = 0; component < clampedCount; ++component)
        {
            destAsInt[component] = (v[component] != static_cast<T>(0) ? GL_TRUE : GL_FALSE);
        }
    }
    else
    {
        updateSamplerUniform(locationInfo, destPointer, clampedCount, v);
        memcpy(destPointer, v, sizeof(T) * clampedCount);
    }

    return count;
}

template <size_t cols, size_t rows, typename T>
GLsizei Program::setMatrixUniformInternal(GLint location,
                                          GLsizei count,
                                          GLboolean transpose,
                                          const T *v)
{
    if (!transpose)
    {
        return setUniformInternal(location, count, cols * rows, v);
    }

    // Perform a transposing copy.
    const VariableLocation &locationInfo = mState.mUniformLocations[location];
    LinkedUniform *linkedUniform         = &mState.mUniforms[locationInfo.index];
    T *destPtr = reinterpret_cast<T *>(linkedUniform->getDataPtrToElement(locationInfo.element));

    // OpenGL ES 3.0.4 spec pg 67: "Values for any array element that exceeds the highest array
    // element index used, as reported by GetActiveUniform, will be ignored by the GL."
    unsigned int remainingElements = linkedUniform->elementCount() - locationInfo.element;
    GLsizei clampedCount           = std::min(count, static_cast<GLsizei>(remainingElements));

    for (GLsizei element = 0; element < clampedCount; ++element)
    {
        size_t elementOffset = element * rows * cols;

        for (size_t row = 0; row < rows; ++row)
        {
            for (size_t col = 0; col < cols; ++col)
            {
                destPtr[col * rows + row + elementOffset] = v[row * cols + col + elementOffset];
            }
        }
    }

    return clampedCount;
}

template <typename DestT>
void Program::getUniformInternal(GLint location, DestT *dataOut) const
{
    const VariableLocation &locationInfo = mState.mUniformLocations[location];
    const LinkedUniform &uniform         = mState.mUniforms[locationInfo.index];

    const uint8_t *srcPointer = uniform.getDataPtrToElement(locationInfo.element);

    GLenum componentType = VariableComponentType(uniform.type);
    if (componentType == GLTypeToGLenum<DestT>::value)
    {
        memcpy(dataOut, srcPointer, uniform.getElementSize());
        return;
    }

    int components = VariableComponentCount(uniform.type);

    switch (componentType)
    {
        case GL_INT:
            UniformStateQueryCastLoop<GLint>(dataOut, srcPointer, components);
            break;
        case GL_UNSIGNED_INT:
            UniformStateQueryCastLoop<GLuint>(dataOut, srcPointer, components);
            break;
        case GL_BOOL:
            UniformStateQueryCastLoop<GLboolean>(dataOut, srcPointer, components);
            break;
        case GL_FLOAT:
            UniformStateQueryCastLoop<GLfloat>(dataOut, srcPointer, components);
            break;
        default:
            UNREACHABLE();
    }
}

bool Program::samplesFromTexture(const gl::State &state, GLuint textureID) const
{
    // Must be called after samplers are validated.
    ASSERT(mCachedValidateSamplersResult.valid() && mCachedValidateSamplersResult.value());

    for (const auto &binding : mState.mSamplerBindings)
    {
        GLenum textureType = binding.textureType;
        for (const auto &unit : binding.boundTextureUnits)
        {
            GLenum programTextureID = state.getSamplerTextureId(unit, textureType);
            if (programTextureID == textureID)
            {
                // TODO(jmadill): Check for appropriate overlap.
                return true;
            }
        }
    }

    return false;
}

}  // namespace gl
