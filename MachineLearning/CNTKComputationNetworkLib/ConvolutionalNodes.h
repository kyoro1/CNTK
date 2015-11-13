//
// <copyright file="ConvolutionalNodes.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//
#pragma once

#include "Basics.h"
#include "Matrix.h"
#include "ComputationNode.h"
#include "InputAndParamNodes.h"
#include "ConvolutionEngine.h"

#include <unordered_set>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <list>
#include <memory>
#include <algorithm>
#include <assert.h>
#include <atomic>
#include <sstream>
#include <iostream>

namespace Microsoft { namespace MSR { namespace CNTK {

    // -----------------------------------------------------------------------
    // ConvolutionNode (convolutionWeights, inputFeature)
    // -----------------------------------------------------------------------

    // convolutional network 
    // This follows "high performance convolutional neural networks for document processing" by Kumar Chellapilla, Sidde Puri, and Patrice Simard.
    // Each sample is stored as a column-major matrix (height, width) of float[numChannels] (r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11).
    template<class ElemType>
    class ConvolutionNode : public ComputationNode<ElemType>, public NumInputs<2>
    {
        typedef ComputationNode<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"Convolution"; }
    public:
        ConvolutionNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            m_kernelWidth(SIZE_MAX), m_kernelHeight(SIZE_MAX),
            // initialize to dummy values so we catch missing initialization
            m_horizontalSubsample(SIZE_MAX), m_verticalSubsample(SIZE_MAX),
            m_zeroPadding(false), m_maxTempMemSizeInSamples(SIZE_MAX)
        {
            m_sampleLayout = ImageLayoutWHC(1, 1, 0);           // TODO: what is this magic #channels == 0? Can this even be initialized at this time, or only inferred?
        }
        ConvolutionNode(DEVICEID_TYPE deviceId, const wstring & name, const size_t kernelWidth, const size_t kernelHeight, const size_t outputChannels, const size_t horizontalSubsample, const size_t verticalSubsample, const bool zeroPadding = false, const size_t maxTempMemSizeInSamples = 0) :
            Base(deviceId, name),
            m_kernelWidth(kernelWidth), m_kernelHeight(kernelHeight),
            m_horizontalSubsample(horizontalSubsample), m_verticalSubsample(verticalSubsample),
            m_zeroPadding(zeroPadding), m_maxTempMemSizeInSamples(maxTempMemSizeInSamples)
        {
            m_sampleLayout = ImageLayoutWHC(1, 1, outputChannels);
            m_factory = ConvolutionEngineFactory<ElemType>::Create(deviceId);
        }
        ConvolutionNode(const ScriptableObjects::IConfigRecordPtr configp) :
            ConvolutionNode(configp->Get(L"deviceId"), L"<placeholder>", configp->Get(L"kernelWidth"), configp->Get(L"kernelHeight"), configp->Get(L"outputChannels"),
                            configp->Get(L"horizontalSubsample"), configp->Get(L"verticalSubsample"),
                            configp->Get(L"zeroPadding"), configp->Get(L"maxTempMemSizeInSamples"))
        {
            // weightNodeName, inputValueNodeName, kernelWidth, kernelHeight, outputChannels, horizontalSubsample, verticalSubsample, zeroPadding = false, maxTempMemSizeInSamples = 0
            AttachInputs(configp, this->GetExpectedNumInputs());
        }

        virtual void Save(File& fstream) const override
        {
            Base::Save(fstream);
            fstream <<  m_kernelWidth << m_kernelHeight << m_horizontalSubsample << m_verticalSubsample;
            fstream << m_sampleLayout.GetNumChannels();
            fstream << m_zeroPadding << m_maxTempMemSizeInSamples;
        }

        virtual void Load(File& fstream, size_t modelVersion) override
        {
            Base::Load(fstream, modelVersion);
            fstream >> m_kernelWidth >> m_kernelHeight >> m_horizontalSubsample >> m_verticalSubsample; 
            size_t outputChannels;
            fstream >> outputChannels;
            m_sampleLayout = ImageLayoutWHC(1, 1, outputChannels);
            fstream >> m_zeroPadding >> m_maxTempMemSizeInSamples;
        }

        virtual void CopyTo(ComputationNodeBasePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const override
        {
            Base::CopyTo(nodeP, newName, flags);
            if (flags & CopyNodeFlags::copyNodeValue)
            {
                auto node = dynamic_pointer_cast<ConvolutionNode<ElemType>>(nodeP);
                node->m_kernelWidth = m_kernelWidth;
                node->m_kernelHeight = m_kernelHeight;

                node->m_horizontalSubsample = m_horizontalSubsample;
                node->m_verticalSubsample = m_verticalSubsample;

                node->m_zeroPadding = m_zeroPadding;

                node->m_maxTempMemSizeInSamples = m_maxTempMemSizeInSamples;

                *node->m_tempMatrix = *m_tempMatrix;
            }
        }

        virtual void /*ComputationNode::*/BackpropTo(const size_t inputIndex, const FrameRange & fr) override
        {
            //if (frameRange.IsAllFrames()) { ComputeInputPartialMap(inputIndex); return; } // TODO: remove these one by one

            //Matrix<ElemType> sliceOutputGrad = GradientSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));
            //Matrix<ElemType> sliceInput1Value = Inputs(1)->ValueSlice(frameRange/*TODO: delete this:*/.Check_t(Inputs(1)->GetNumParallelSequences(), m_pMBLayout));

            //if (inputIndex == 0)  //derivative with regard to the weight matrix
            //    ComputeInputPartialOverWeight(sliceOutputGrad, Inputs(0)->GradientValues(), Inputs(0)->FunctionValues(), sliceInput1Value, *m_tempMatrix, !frameRange.IsAllFrames());
            //else if (inputIndex == 1)  // derivative with regard to the input feature
            //{
            //    Matrix<ElemType> sliceInput1Grad = Inputs(1)->GradientSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));
            //    ComputeInputPartialOverInputFeature(sliceOutputGrad, sliceInput1Grad, Inputs(0)->FunctionValues(), sliceInput1Value, *m_tempMatrix);
            //}

            Matrix<ElemType> sliceOutputGrad = GradientSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));
            Matrix<ElemType> sliceInput1Value = Inputs(1)->ValueSlice(frameRange/*TODO: delete this:*/.Check_t(Inputs(1)->GetNumParallelSequences(), m_pMBLayout));

            size_t batchSize = m_pMBLayout->GetNumParallelSequences();
            m_inT->setN(batchSize);
            m_outT->setN(batchSize);
            assert(m_convEng != nullptr);
            if (inputIndex == 0)  //derivative with regard to the weight matrix
            {
                Matrix<ElemType>& grad = Inputs(0)->GradientValues();
                m_convEng->BackwardFilter(*m_outT, sliceOutputGrad, *m_inT, sliceInput1Value, *m_convDesc, *m_filterT, grad, frameRange.IsAllFrames());
            }
            else if (inputIndex == 1)  // derivative with regard to the input feature
            {
                const Matrix<ElemType>& input0 = Inputs(0)->FunctionValues();
                Matrix<ElemType> sliceInput1Grad = Inputs(1)->GradientSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));
                m_convEng->BackwardData(*m_outT, sliceOutputGrad, *m_filterT, input0, *m_convDesc, *m_inT, sliceInput1Grad);
            }
        }

        virtual bool OutputUsedInComputingInputNodesGradients() const override
        {
            // The ConvolutionNode does not require its output value for computing
            // the gradients of its input nodes
            return false;
        }

    private:
        void BackpropToOverWeight(Matrix<ElemType> &gradientValues,
            Matrix<ElemType> &inputGradientValues, const Matrix<ElemType> &/*input0*/, const Matrix<ElemType> &input1, Matrix<ElemType> &tempMatrix, const bool inLoop)
        {
            //UNUSED(inLoop);
            //UNUSED(tempMatrix);
            //size_t batchSize = m_pMBLayout->GetNumParallelSequences();
            //m_inT->setN(batchSize);
            //m_outT->setN(batchSize);
            //assert(m_convEng != nullptr);
            //m_convEng->BackwardFilter(*m_outT, gradientValues, *m_inT, input1, *m_convDesc, *m_filterT, inputGradientValues, !inLoop);

            size_t packedInputRows = m_kernelWidth * m_kernelHeight * m_inputImageLayout.channels;
            size_t packedInputColsPerSample = m_outputImageLayout.width * m_outputImageLayout.height;
            size_t outputSizePerChannel = packedInputColsPerSample;
            //size_t packedInputDim = packedInputRows * packedInputColsPerSample; // size of each packed input sample
            //size_t inputDim = m_inputImageLayout.width * m_inputImageLayout.height * m_inputImageLayout.channels;  //size of each input sample

            size_t batchSize = input1.GetNumCols(); //right child is the input sample

            size_t maxTempMemSizeInSamples = (m_maxTempMemSizeInSamples == 0 ? batchSize : m_maxTempMemSizeInSamples);

            //const Matrix<ElemType> & weightMatrix = input0;
            //inputGradientValues.Resize(weightMatrix.GetNumRows(), weightMatrix.GetNumCols()); //should have been resized when preparing gradient computation

            gradientValues.Reshape(m_outputImageLayout.channels, outputSizePerChannel * batchSize);  //reshape to match the longernal operation

            size_t subBatchSize = min(batchSize, maxTempMemSizeInSamples);
            size_t numSubBatches = (batchSize + subBatchSize - 1) / subBatchSize;

            // REVIEW alexeyk: temp commented out.
            UNUSED(inLoop);
            //if (numSubBatches == 1 && !inLoop && !m_1DConvolutionOnGPUSparse)  //reuse packed input from evaluation step if it's not changed by either subbatch or recurrent steps or special 1-D convolution for text.
            //    Matrix<ElemType>::MultiplyAndAdd(gradientValues, false, tempMatrix, true, inputGradientValues);
            //else
            //{
            //    for (size_t i = 0; i<numSubBatches; i++)
            //    {
            //        size_t startSampleID = i*subBatchSize;
            //        size_t endSampleID = min(batchSize, startSampleID + subBatchSize);
            //        size_t smallBatchSize = endSampleID - startSampleID;

            //        tempMatrix.Resize(packedInputRows, packedInputColsPerSample * smallBatchSize);
            //        Matrix<ElemType> inputSubBatch = input1.ColumnSlice(startSampleID, smallBatchSize);
            //        inputSubBatch.SwitchToMatrixType(MatrixType::DENSE, inputSubBatch.GetFormat(), true);
            //        tempMatrix.AssignPackedConvolutionInput(inputSubBatch,
            //                                                m_inputImageLayout.width, m_inputImageLayout.height, m_inputImageLayout.channels,
            //                                                m_outputImageLayout.width, m_outputImageLayout.height, m_outputImageLayout.channels,
            //                                                m_kernelWidth, m_kernelHeight, m_horizontalSubsample, m_verticalSubsample,
            //                                                m_zeroPadding);

            //        Matrix<ElemType> outputGradientSubBatch = gradientValues.ColumnSlice(startSampleID * outputSizePerChannel, smallBatchSize * outputSizePerChannel);
            //        Matrix<ElemType>::MultiplyAndAdd(outputGradientSubBatch, false, tempMatrix, true, inputGradientValues);
            //    }
            //}

            gradientValues.Reshape(m_outputImageLayout.channels * outputSizePerChannel, batchSize);  //change back
        }

        //compute gradient over the packed input and then convert the result to the original input
        void BackpropToOverInputFeature(Matrix<ElemType> &gradientValues, const Matrix<ElemType> &inputGradientValues, const Matrix<ElemType> &input0, const Matrix<ElemType> &input1, Matrix<ElemType> &tempMatrix)
        {
            //UNUSED(input1);
            //UNUSED(tempMatrix);
            //// REVIEW alexeyk: setting batch size, can it be done elsewhere in a single place?
            //size_t batchSize = m_pMBLayout->GetNumParallelSequences();
            //m_inT->setN(batchSize);
            //m_outT->setN(batchSize);
            //assert(m_convEng != nullptr);
            //m_convEng->BackwardData(*m_outT, gradientValues, *m_filterT, input0, *m_convDesc, *m_inT, inputGradientValues);

            size_t packedInputRows = m_kernelWidth * m_kernelHeight * m_inputImageLayout.channels;
            size_t packedInputColsPerSample = m_outputImageLayout.width * m_outputImageLayout.height;
            size_t outputSizePerChannel = packedInputColsPerSample;
            //size_t packedInputDim = packedInputRows * packedInputColsPerSample; // size of each packed input sample
            //size_t inputDim = m_inputImageLayout.width * m_inputImageLayout.height * m_inputImageLayout.channels;  //size of each input sample

            size_t batchSize = input1.GetNumCols(); //right child is the input sample

            size_t maxTempMemSizeInSamples = (m_maxTempMemSizeInSamples == 0 ? batchSize : m_maxTempMemSizeInSamples);

            const Matrix<ElemType> & weightMatrix = input0;

            gradientValues.Reshape(m_outputImageLayout.channels, outputSizePerChannel * batchSize);  //reshape to match the longernal operation

            size_t subBatchSize = min(batchSize, maxTempMemSizeInSamples);
            size_t numSubBatches = (batchSize + subBatchSize - 1) / subBatchSize;

            for (size_t i = 0; i<numSubBatches; i++)
            {
                size_t startSampleID = i*subBatchSize;
                size_t endSampleID = min(batchSize, startSampleID + subBatchSize);
                size_t smallBatchSize = endSampleID - startSampleID;

                tempMatrix.Resize(packedInputRows, packedInputColsPerSample * smallBatchSize);
                Matrix<ElemType> outputGradientSubBatch = gradientValues.ColumnSlice(startSampleID * outputSizePerChannel, smallBatchSize * outputSizePerChannel);
                Matrix<ElemType>::Multiply(weightMatrix, true, outputGradientSubBatch, false, tempMatrix);

                Matrix<ElemType> inputGradientSubBatch = inputGradientValues.ColumnSlice(startSampleID, smallBatchSize);
                tempMatrix.UnpackConvolutionInput(inputGradientSubBatch,
                                                  m_inputImageLayout.width, m_inputImageLayout.height, m_inputImageLayout.channels,
                                                  m_outputImageLayout.width, m_outputImageLayout.height, m_outputImageLayout.channels,
                                                  m_kernelWidth, m_kernelHeight, m_horizontalSubsample, m_verticalSubsample,
                                                  m_zeroPadding);
            }

            gradientValues.Reshape(m_outputImageLayout.channels * outputSizePerChannel, batchSize);  //change back
        }
    public:

        virtual void /*ComputationNode::*/ForwardProp(const FrameRange & fr) override
        {
            //if (frameRange.IsAllFrames()) { EvaluateThisNodeMap(); return; }

            //Matrix<ElemType> sliceInput1Value = Inputs(1)->ValueSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));
            //Matrix<ElemType> sliceOutputValue = ValueSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));
            //EvaluateThisNodeS(sliceOutputValue, Inputs(0)->FunctionValues(), sliceInput1Value, *m_tempMatrix);

            const Matrix<ElemType>& input0 = Inputs(0)->FunctionValues();
            Matrix<ElemType> sliceInput1Value = Inputs(1)->ValueSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));
            Matrix<ElemType> sliceOutputValue = ValueSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));
            // REVIEW alexeyk: setting batch size, can it be done elsewhere in a single place?
            size_t batchSize = m_pMBLayout->GetNumParallelSequences();
            m_inT->setN(batchSize);
            m_outT->setN(batchSize);
            assert(m_convEng != nullptr);
#if NANCHECK
            input0.HasNan("Convolution-input0");
            sliceInput1Value.HasNan("Convolution-input1");
#endif
            m_convEng->Forward(*m_inT, sliceInput1Value, *m_filterT, input0, *m_convDesc, *m_outT, sliceOutputValue);
#if NANCHECK
            sliceOutputValue.HasNan("Convolution");
#endif
        }

        void AddBias(const Matrix<ElemType>& output, const Matrix<ElemType>& bias, Matrix<ElemType>& dst)
        {
            assert(m_convEng != nullptr);
            m_convEng->AddBias(*m_outT, output, *m_biasT, bias, dst);
        }

        void BackwardBias(const Matrix<ElemType>& srcGrad, Matrix<ElemType>& biasGrad)
        {
            assert(m_convEng != nullptr);
            m_convEng->BackwardBias(*m_outT, srcGrad, *m_biasT, biasGrad);
        }

    private:
        void ForwardPropS(Matrix<ElemType> &functionValues, const Matrix<ElemType> &input0, 
                               const Matrix<ElemType> &input1, Matrix<ElemType> &tempMatrix)
        {
#if NANCHECK
            input0.HasNan("Convolution-input0");
            input1.HasNan("Convolution-input1");
#endif
            size_t packedInputRows = m_kernelWidth * m_kernelHeight * m_inputImageLayout.channels;
            size_t packedInputColsPerSample = m_outputImageLayout.width * m_outputImageLayout.height;
            size_t outputSizePerChannel = packedInputColsPerSample;
            //size_t packedInputDim = packedInputRows * packedInputColsPerSample; // size of each packed input sample
            //size_t inputDim = m_inputImageLayout.width * m_inputImageLayout.height * m_inputImageLayout.channels;  //size of each input sample

            //size_t packedInputRows = m_kernelWidth * m_kernelHeight * m_inputImageLayout.GetNumChannels();
            //size_t packedInputColsPerSample = m_imageLayout.GetWidth() * m_imageLayout.GetHeight();
            //size_t outputSizePerChannel = packedInputColsPerSample;
            ////size_t packedInputDim = packedInputRows * packedInputColsPerSample; // size of each packed input sample
            ////size_t inputDim = m_inputImageLayout.GetWidth() * m_inputImageLayout.GetHeight() * m_inputImageLayout.GetNumChannels();  //size of each input sample

            size_t maxTempMemSizeInSamples = (m_maxTempMemSizeInSamples == 0? batchSize : m_maxTempMemSizeInSamples);

            const Matrix<ElemType> & weightMatrix = input0;
            assert(weightMatrix.GetNumCols() == packedInputRows && weightMatrix.GetNumRows() == m_outputImageLayout.channels);
            functionValues.Resize(m_outputImageLayout.channels, outputSizePerChannel * batchSize);

            //const Matrix<ElemType> & weightMatrix = input0;
            //assert(weightMatrix.GetNumCols() == packedInputRows && weightMatrix.GetNumRows() == m_imageLayout.GetNumChannels());

            // GPU and 1-dimensional image
            //bool m_1DConvolutionOnGPUSparse = (m_inputImageLayout.GetHeight() == 1
            //    && input1.GetCurrentMatrixLocation() == CurrentDataLocation::GPU
            //    && input1.GetMatrixType() == MatrixType::SPARSE);

            //functionValues.SwitchToMatrixType(MatrixType::DENSE, MatrixFormat::matrixFormatDense, false);

            // Reshaping is only necessary if we are going to use the unpacking trick
            //if (!m_1DConvolutionOnGPUSparse)
            //    functionValues.Reshape(m_imageLayout.GetNumChannels(), outputSizePerChannel * batchSize);

            //size_t subBatchSize = min(batchSize, maxTempMemSizeInSamples); 
            //size_t numSubBatches = (batchSize+subBatchSize-1)/subBatchSize; 

            //for (size_t i=0; i<numSubBatches; i++) 
            //{
            //    size_t startSampleID = i*subBatchSize; 
            //    size_t endSampleID = min(batchSize, startSampleID + subBatchSize); 
            //    size_t smallBatchSize = endSampleID-startSampleID;
            //    Matrix<ElemType>  inputSubBatch;

                // We optimize for three different scenarios here by handling them slightly differently.
                // [Scenario 1] Dense: Unroll using AssignPackedConvolutionInput and multiply.
                // [Scenario 2] Sparse 1-D convolution on GPU: for text scenarios we have a specific kernel.
                // [Scenario 3] Sparse all others: convert to dense. Temporary work-around - allocating/de-allocating memory is costly!
            //    if (input1.GetMatrixType() == MatrixType::DENSE || m_1DConvolutionOnGPUSparse)
            //    {
            //        inputSubBatch = input1.ColumnSlice(startSampleID, smallBatchSize);
            //    }
            //    else
            //    {
            //        inputSubBatch.SetValue(input1.ColumnSlice(startSampleID, smallBatchSize), input1.GetFormat());
            //        inputSubBatch.SwitchToMatrixType(MatrixType::DENSE, MatrixFormat::matrixFormatDense, true);
            //    }

            //    if (m_1DConvolutionOnGPUSparse)
            //    {
            //        if (m_kernelWidth * m_inputImageLayout.GetNumChannels() != weightMatrix.GetNumCols())
            //            LogicError("Kernel width and weight matrix dimensions don't match.");

            //        Matrix<ElemType>  outputSubBatch = functionValues.ColumnSlice(startSampleID, smallBatchSize);
            //        Matrix<ElemType>::ConvolveAndWeightedAdd(1, weightMatrix, inputSubBatch, 0, outputSubBatch,
            //            m_horizontalSubsample * m_inputImageLayout.GetNumChannels(), m_zeroPadding);
            //    }
            //    else
            //    {
            //        Matrix<ElemType>  outputSubBatch = functionValues.ColumnSlice(outputSizePerChannel * startSampleID, outputSizePerChannel * smallBatchSize);
            //        tempMatrix.Resize(packedInputRows, packedInputColsPerSample * smallBatchSize);
            //        tempMatrix.AssignPackedConvolutionInput(inputSubBatch,
            //                                            m_inputImageLayout.GetWidth(), m_inputImageLayout.GetHeight(), m_inputImageLayout.GetNumChannels(),
            //                                            m_imageLayout.GetWidth(), m_imageLayout.GetHeight(), m_imageLayout.GetNumChannels(),
            //                                            m_kernelWidth, m_kernelHeight, m_horizontalSubsample, m_verticalSubsample, m_zeroPadding);

            //        Matrix<ElemType>::Multiply(weightMatrix, false, tempMatrix, false, outputSubBatch);
            //    }
            //}

            //functionValues.Reshape(m_imageLayout.GetNumChannels() * outputSizePerChannel, batchSize);  //each sample becomes a column

#if NANCHECK
            functionValues.HasNan("Convolution");
#endif
        }
    public:

        // note: this also infers dimensions from chilren
        virtual void /*ComputationNodeBase::*/Validate(bool isFinalValidationPass) override
        {
            Base::Validate(isFinalValidationPass);

            if (m_horizontalSubsample > m_kernelWidth || m_verticalSubsample > m_kernelHeight)
                InvalidArgument("In ConvolutionNode horizontalSubsample must <= kernelWidth and verticalSubsample must <= kernelHeight.");

            InferMBLayoutFromInputsForStandardCase();
            InferImageDimsFromInputs();

            size_t weightCols = m_kernelWidth * m_kernelHeight * m_inputSampleLayout.GetNumChannels();

            if (Input(0)->Value().HasNoElements())
                ValidateInferInputDims(0, m_sampleLayout.GetNumChannels(), weightCols);

            if (isFinalValidationPass && (Input(0)->GetNumCols() != weightCols || Input(0)->GetNumRows() != m_sampleLayout.GetNumChannels()))
                LogicError("convolutionWeight matrix %ls should have dimension [%d, %d] which is [outputChannels, kernelWidth * kernelHeight * inputChannels]", m_inputs[0]->NodeName().c_str(), (int)m_sampleLayout.GetNumChannels(), (int)weightCols);

            size_t inputDim = m_inputSampleLayout.GetWidth() * m_inputSampleLayout.GetHeight() * m_inputSampleLayout.GetNumChannels();
            if (Input(1)->GetNumRows() == 0)
                ValidateInferInputDims(1, inputDim, Input(1)->GetNumCols());

            if (isFinalValidationPass && Input(1)->GetNumRows() != inputDim)
                LogicError("each column of input to the convolution node %ls is a sample and should have dimension %d, which is inputWidth * inputHeight * inputChannels", NodeName().c_str(), (int)inputDim);

            size_t outputDim = m_sampleLayout.GetWidth() * m_sampleLayout.GetHeight() * m_sampleLayout.GetNumChannels();
            SetDims(outputDim, Input(1)->GetNumCols());
        }

        virtual void InferImageDimsFromInputs()
        {
            InferImageDimsFromInput(1, false);

            if (m_inputSampleLayout.GetWidth() < m_kernelWidth || m_inputSampleLayout.GetHeight() < m_kernelHeight)
                InvalidArgument("inputWidth must >= kernelWidth and inputHeight must >= kernelHeight.");

            if (m_zeroPadding)
            {
                const int kernelWidthCenter = m_kernelWidth % 2;
                const int kernelHeightCenter = m_kernelHeight % 2;
                m_sampleLayout = ImageLayoutWHC((m_inputSampleLayout.GetWidth()  - kernelWidthCenter)  / m_horizontalSubsample + 1,
                                                     (m_inputSampleLayout.GetHeight() - kernelHeightCenter) / m_verticalSubsample   + 1,
                                                     m_sampleLayout.GetNumChannels());
            }
            else
            {
                m_sampleLayout = ImageLayoutWHC((m_inputSampleLayout.GetWidth()  - m_kernelWidth)  / m_horizontalSubsample + 1,
                                                     (m_inputSampleLayout.GetHeight() - m_kernelHeight) / m_verticalSubsample   + 1,
                                                     m_sampleLayout.GetNumChannels());
            }    

            // REVIEW alexeyk: is there a better place to create engines?
            if (m_factory == nullptr)
                m_factory = ConvolutionEngineFactory<ElemType>::Create(m_deviceId);
            if (m_convEng == nullptr)
                m_convEng = m_factory->CreateConvEngine(m_maxTempMemSizeInSamples);
            if (m_inT == nullptr)
                m_inT = m_factory->CreateTensor(m_inputImageLayout.width, m_inputImageLayout.height, m_inputImageLayout.channels, 1);
            if (m_filterT == nullptr)
                m_filterT = m_factory->CreateFilter(m_kernelWidth, m_kernelHeight, m_inputImageLayout.channels, m_outputImageLayout.channels);
            if (m_outT == nullptr)
                m_outT = m_factory->CreateTensor(m_outputImageLayout.width, m_outputImageLayout.height, m_outputImageLayout.channels, 1);
            if (m_convDesc == nullptr)
                m_convDesc = m_factory->CreateConvDescriptor(*m_inT, *m_filterT, m_horizontalSubsample, m_verticalSubsample, m_zeroPadding);
            // REVIEW alexeyk: create per-channel (shared) bias. Consider adding other types of biases.
            if (m_biasT == nullptr)
                m_biasT = m_factory->CreateTensor(1, 1, m_outputImageLayout.channels, 1);
        }

        virtual void DumpNodeInfo(const bool printValues, File& fstream) const override
        {
            Base::DumpNodeInfo(printValues, fstream);

            char str[4096];
            sprintf(str, "Input[Width:%lu, Height:%lu, Channels:%lu]  \n", m_inputSampleLayout.GetWidth(), m_inputSampleLayout.GetHeight(), m_inputSampleLayout.GetNumChannels());
            fstream << string(str);
            sprintf(str, "Kernel[Width:%lu, Height:%lu]  SubSample[Horizontal:%lu, Vertical:%lu]\n", m_kernelWidth, m_kernelHeight, m_horizontalSubsample, m_verticalSubsample);
            fstream << string(str);
            sprintf(str, "Output[Width:%lu, Height:%lu, Channels:%lu]  \n", m_sampleLayout.GetWidth(), m_sampleLayout.GetHeight(), m_sampleLayout.GetNumChannels());
            fstream << string(str);
            sprintf(str, "ZeroPadding=%ls  maxTempMemSizeInSamples=%lu\n", m_zeroPadding? L"true" : L"false", m_maxTempMemSizeInSamples);
            fstream << string(str);
        }

        void SetmMaxTempMemSizeInSamples(const size_t maxTempMemSizeInSamples)
        {
            m_maxTempMemSizeInSamples = maxTempMemSizeInSamples;
        }

        //request matrices needed to do node function value evaluation
        virtual void RequestMatricesBeforeForwardProp(MatrixPool& matrixPool)
        {
            Base::RequestMatricesBeforeForwardProp(matrixPool);
            RequestMatrixFromPool(m_tempMatrix, matrixPool);
        }

        //release gradient and temp matrices that no longer needed after all the children's gradients are computed.
        virtual void ReleaseMatricesAfterBackprop(MatrixPool& matrixPool)
        {
            Base::ReleaseMatricesAfterBackprop(matrixPool);
            ReleaseMatrixToPool(m_tempMatrix, matrixPool);
        }

    private:
        std::unique_ptr<ConvolutionEngineFactory<ElemType>> m_factory;
        std::unique_ptr<ConvolutionEngine<ElemType>> m_convEng;

        std::unique_ptr<ConvolutionTensor4D> m_inT;
        std::unique_ptr<ConvolutionFilter> m_filterT;
        std::unique_ptr<ConvolutionTensor4D> m_outT;
        std::unique_ptr<ConvolutionDescriptor> m_convDesc;
        std::unique_ptr<ConvolutionTensor4D> m_biasT;

        size_t m_kernelWidth, m_kernelHeight;
        size_t m_horizontalSubsample, m_verticalSubsample;
        bool m_zeroPadding;
        bool m_1DConvolutionOnGPUSparse;

        shared_ptr<Matrix<ElemType>> m_tempMatrix;
        size_t m_maxTempMemSizeInSamples; // can change during runtime
    };

    template class ConvolutionNode<float>; 
    template class ConvolutionNode<double>;

    // -----------------------------------------------------------------------
    // PoolingNodeBase (input)
    // -----------------------------------------------------------------------

    // Max/Average Pooling: support multi channel
    // Each sample is stored as a column-major matrix (height, width) of float[numChannels] (r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11).
    template<class ElemType>
    class PoolingNodeBase : public ComputationNode<ElemType>, public NumInputs<1>
    {
        typedef ComputationNode<ElemType> Base; UsingComputationNodeMembers;
    public:
        PoolingNodeBase(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name),
            m_windowWidth(SIZE_MAX), m_windowHeight(SIZE_MAX),
            m_horizontalSubsample(SIZE_MAX), m_verticalSubsample(SIZE_MAX)
        { }
        PoolingNodeBase(DEVICEID_TYPE deviceId, const wstring & name, const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample) :
            Base(deviceId, name),
            m_windowWidth(windowWidth), m_windowHeight(windowHeight),
            m_horizontalSubsample(horizontalSubsample), m_verticalSubsample(verticalSubsample)
        {
            m_factory = ConvolutionEngineFactory<ElemType>::Create(deviceId);
        }
        PoolingNodeBase(const ScriptableObjects::IConfigRecordPtr configp) :
            PoolingNodeBase(configp->Get(L"deviceId"), L"<placeholder>", configp->Get(L"windowWidth"), configp->Get(L"windowHeight"), configp->Get(L"horizontalSubsample"), configp->Get(L"verticalSubsample"))
        {
            // input, windowWidth, windowHeight, horizontalSubsample, verticalSubsample
            AttachInputs(configp, this->GetExpectedNumInputs());
        }

        virtual void Save(File& fstream) const override
        {
            Base::Save(fstream);
            fstream << m_windowWidth << m_windowHeight << m_horizontalSubsample << m_verticalSubsample;
        }

        virtual void Load(File& fstream, size_t modelVersion) override
        {
            Base::Load(fstream, modelVersion);
            fstream >> m_windowWidth >> m_windowHeight >> m_horizontalSubsample >> m_verticalSubsample;
        }

        virtual void CopyTo(ComputationNodeBasePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const override
        {
            Base::CopyTo(nodeP, newName, flags);
            if (flags & CopyNodeFlags::copyNodeValue)
            {
                auto node = dynamic_pointer_cast<PoolingNodeBase<ElemType>>(nodeP);

                node->m_windowWidth = m_windowWidth;
                node->m_windowHeight = m_windowHeight;

                node->m_horizontalSubsample = m_horizontalSubsample;
                node->m_verticalSubsample = m_verticalSubsample;

                node->m_inputSizePerSample = m_inputSizePerSample;
                node->m_outputSizePerSample = m_outputSizePerSample;
            }
        }

        virtual void /*ComputationNode::*/BackpropTo(const size_t /*inputIndex*/, const FrameRange & fr) override
        {
            Matrix<ElemType> sliceInput0Grad = Input(0)->GradientFor(fr);
            Matrix<ElemType> sliceOutputGrad = GradientFor(fr);

            Matrix<ElemType> sliceInput0Value = Input(0)->ValueFor(fr);
            Matrix<ElemType> sliceOutputValue = ValueFor(fr);

            size_t batchSize = m_pMBLayout->GetNumParallelSequences();
            m_inT->setN(batchSize);
            m_outT->setN(batchSize);
            assert(m_poolEng != nullptr);
            assert(m_poolDesc != nullptr);
            m_poolEng->Backward(*m_outT, sliceOutputValue, sliceOutputGrad, *m_poolDesc, *m_inT, sliceInput0Value, sliceInput0Grad);
        }

        virtual void EvaluateThisNode(const FrameRange & frameRange) override
        {
            //if (frameRange.IsAllFrames()) { EvaluateThisNodeMap(); return; }
            Matrix<ElemType> sliceInput0Value = Inputs(0)->ValueSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));
            Matrix<ElemType> sliceOutputValue = ValueSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));

            size_t batchSize = m_pMBLayout->GetNumParallelSequences();
            m_inT->setN(batchSize);
            m_outT->setN(batchSize);
            assert(m_poolEng != nullptr);
            assert(m_poolDesc != nullptr);
            m_poolEng->Forward(*m_inT, sliceInput0Value, *m_poolDesc, *m_outT, sliceOutputValue);
        }

        virtual void Validate(bool isFinalValidationPass) override
        {
            Base::Validate(isFinalValidationPass);

            if (m_horizontalSubsample > m_windowWidth || m_verticalSubsample > m_windowHeight)
                InvalidArgument("PoolingNodeBase: horizontalSubsample must <= windowWidth and verticalSubsample must <= windowHeight.");

            InferMBLayoutFromInputsForStandardCase();
            InferImageDimsFromInputs();

            m_inputSizePerSample = m_inputSampleLayout.GetWidth() * m_inputSampleLayout.GetHeight() * m_inputSampleLayout.GetNumChannels();
            m_outputSizePerSample = m_sampleLayout.GetWidth() * m_sampleLayout.GetHeight() * m_sampleLayout.GetNumChannels();

            if (Input(0)->GetNumRows() == 0)
                ValidateInferInputDims(0, m_inputSizePerSample, Input(0)->GetNumCols());

            if (isFinalValidationPass && Input(0)->GetNumRows() != m_inputSizePerSample)
                LogicError("each column of input to the MaxPooling node %ls is a sample and should have dimension %d, which is inputWidth * inputHeight * inputChannels", NodeName().c_str(), (int)m_inputSizePerSample);

            SetDims(m_outputSizePerSample, Input(0)->GetNumCols());
        }

        virtual void InferImageDimsFromInputs() override
        {
            InferImageDimsFromInput(0, false);

            if (m_inputSampleLayout.GetWidth() < m_windowWidth || m_inputSampleLayout.GetHeight() < m_windowHeight)
                InvalidArgument("PoolingNodeBase: inputWidth must >= windowWidth and inputHeight must >= windowHeight.");

            m_sampleLayout = ImageLayoutWHC((m_inputSampleLayout.GetWidth()  - m_windowWidth)  / m_horizontalSubsample + 1,
                                                 (m_inputSampleLayout.GetHeight() - m_windowHeight) / m_verticalSubsample   + 1,
                                                 m_inputSampleLayout.GetNumChannels());

            // REVIEW alexeyk: is there a better place to create engines?
            if (m_factory == nullptr)
                m_factory = ConvolutionEngineFactory<ElemType>::Create(m_deviceId);
            if (m_poolEng == nullptr)
                m_poolEng = m_factory->CreatePoolEngine();
            if (m_inT == nullptr)
                m_inT = m_factory->CreateTensor(m_inputImageLayout.width, m_inputImageLayout.height, m_inputImageLayout.channels, 1);
            if (m_outT == nullptr)
                m_outT = m_factory->CreateTensor(m_outputImageLayout.width, m_outputImageLayout.height, m_outputImageLayout.channels, 1);
        }

        virtual void DumpNodeInfo(const bool printValues, File& fstream) const override
        {
            Base::DumpNodeInfo(printValues, fstream);

            char str[4096];
            sprintf(str, "Input[Width:%lu, Height:%lu, Channels:%lu]  \n", m_inputSampleLayout.GetWidth(), m_inputSampleLayout.GetHeight(), m_inputSampleLayout.GetNumChannels());
            fstream << string(str);
            sprintf(str, "PoolingWindow[Width:%lu, Height:%lu]  SubSampling[Horizontal:%lu, Vertical:%lu]\n", m_windowWidth, m_windowHeight, m_horizontalSubsample, m_verticalSubsample);
            fstream << string(str);
            sprintf(str, "Output[Width:%lu, Height:%lu, Channels:%lu]  \n", m_sampleLayout.GetWidth(), m_sampleLayout.GetHeight(), m_sampleLayout.GetNumChannels());
            fstream << string(str);
            sprintf(str, "TotalSizePerSample[Input:%lu, Output:%lu]  \n", m_inputSizePerSample, m_outputSizePerSample);
            fstream << string(str);
        }

    protected:
        std::unique_ptr<ConvolutionEngineFactory<ElemType>> m_factory;
        std::unique_ptr<PoolingEngine<ElemType>> m_poolEng;

        std::unique_ptr<ConvolutionTensor4D> m_inT;
        std::unique_ptr<ConvolutionTensor4D> m_outT;
        std::unique_ptr<PoolingDescriptor> m_poolDesc;

        size_t m_windowWidth, m_windowHeight;
        size_t m_horizontalSubsample, m_verticalSubsample;
        size_t m_inputSizePerSample, m_outputSizePerSample;
    };

    // add this at the start of each derived class, to get access to the members of ComputationNode
    // See #define of 'UsingComputationNodeMembersBoilerplate' for more explanation.
#define UsingPoolingNodeBaseMembers UsingComputationNodeMembersBoilerplate; \
    protected:  \
        using Base::m_windowWidth; using Base::m_windowHeight; using Base::m_horizontalSubsample; using Base::m_verticalSubsample; using Base::m_inputSizePerSample; using Base::m_outputSizePerSample; \
    public:

    // -----------------------------------------------------------------------
    // MaxPoolingNode
    // -----------------------------------------------------------------------

    template<class ElemType>
    class MaxPoolingNode : public PoolingNodeBase<ElemType>
    {
        typedef PoolingNodeBase<ElemType> Base; UsingPoolingNodeBaseMembers;
        static const std::wstring TypeName() { return L"MaxPooling"; }
    public:
        MaxPoolingNode(DEVICEID_TYPE deviceId, const wstring & name) : Base(deviceId, name) { }
        MaxPoolingNode(DEVICEID_TYPE deviceId, const wstring & name, const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample) :
            Base(deviceId, name, windowWidth, windowHeight, horizontalSubsample, verticalSubsample)
        { }
        MaxPoolingNode(const ScriptableObjects::IConfigRecordPtr configp) :
            Base(configp)
        { }

        virtual void BackpropToV(const Matrix<ElemType> &gradientValues, Matrix<ElemType> &inputGradientValues, const Matrix<ElemType> &input0, const Matrix<ElemType> &functionValues) override
        {
            inputGradientValues.AddMaxPoolingGradient(gradientValues, input0, functionValues, m_inputSampleLayout.GetNumChannels(),
                                                      m_inputSampleLayout.GetWidth(), m_inputSampleLayout.GetHeight(), m_inputSizePerSample, 
                                                      m_sampleLayout.GetWidth(), m_sampleLayout.GetHeight(), m_outputSizePerSample, 
                                                      m_windowWidth, m_windowHeight, m_horizontalSubsample, m_verticalSubsample);
        }

        virtual void ForwardPropV(Matrix<ElemType> &functionValues, const Matrix<ElemType> &input0) override
        {
            size_t batchSize = m_pMBLayout->GetNumParallelSequences();
            m_inT->setN(batchSize);
            m_outT->setN(batchSize);
            assert(m_poolEng != nullptr);
            m_poolEng->Forward(*m_inT, input0, *m_poolDesc, *m_outT, functionValues);
        }

        void InferImageDimsFromInputs() override
        {
            functionValues.AssignMaxPoolingResult(input0, m_inputSampleLayout.GetNumChannels(),
                                                  m_inputSampleLayout.GetWidth(), m_inputSampleLayout.GetHeight(), m_inputSizePerSample, 
                                                  m_sampleLayout.GetWidth(), m_sampleLayout.GetHeight(), m_outputSizePerSample, 
                                                  m_windowWidth, m_windowHeight, m_horizontalSubsample, m_verticalSubsample);
        }
    };

    template class MaxPoolingNode<float>; 
    template class MaxPoolingNode<double>;    

    // -----------------------------------------------------------------------
    // AveragePoolingNode
    // -----------------------------------------------------------------------

    template<class ElemType>
    class AveragePoolingNode : public PoolingNodeBase<ElemType>
    {
        typedef PoolingNodeBase<ElemType> Base; UsingPoolingNodeBaseMembers;
        static const std::wstring TypeName() { return L"AveragePooling"; }
    public:
        AveragePoolingNode(DEVICEID_TYPE deviceId, const wstring & name) : Base(deviceId, name) { }
        AveragePoolingNode(DEVICEID_TYPE deviceId, const wstring & name, const size_t windowWidth, const size_t windowHeight, const size_t horizontalSubsample, const size_t verticalSubsample) :
            Base(deviceId, name, windowWidth, windowHeight, horizontalSubsample, verticalSubsample)
        { }
        AveragePoolingNode(const ScriptableObjects::IConfigRecordPtr configp) :
            Base(configp)
        { }

        virtual void BackpropToV(const Matrix<ElemType> &gradientValues, Matrix<ElemType> &inputGradientValues, const Matrix<ElemType> &/*input0*/, const Matrix<ElemType> &/*functionValues*/) override
        {
            inputGradientValues.AddAveragePoolingGradient(gradientValues, m_inputSampleLayout.GetNumChannels(),
                                                          m_inputSampleLayout.GetWidth(), m_inputSampleLayout.GetHeight(), m_inputSizePerSample, 
                                                          m_sampleLayout.GetWidth(), m_sampleLayout.GetHeight(), m_outputSizePerSample, 
                                                          m_windowWidth, m_windowHeight, m_horizontalSubsample, m_verticalSubsample);
        }

        virtual bool OutputUsedInComputingInputNodesGradients() const override
        {
            // The AveragePoolingNode does not require its output value for computing
            // the gradients of its input nodes
            return false;
        }

        virtual bool InputUsedInComputingInputNodesGradients(size_t childIndex) const
        {
            // The AveragePoolingNode does not require any of it's input's values for computing
            // the gradients of its input nodes
            UNREFERENCED_PARAMETER(childIndex);
            return false;
        }

        virtual void ForwardPropV(Matrix<ElemType> &functionValues, const Matrix<ElemType> &input0) override
        {
            functionValues.AssignAveragePoolingResult(input0, m_inputSampleLayout.GetNumChannels(),
                                                      m_inputSampleLayout.GetWidth(), m_inputSampleLayout.GetHeight(), m_inputSizePerSample, 
                                                      m_sampleLayout.GetWidth(), m_sampleLayout.GetHeight(), m_outputSizePerSample, 
                                                      m_windowWidth, m_windowHeight, m_horizontalSubsample, m_verticalSubsample);
        void InferImageDimsFromInputs() override
        {
            Base::InferImageDimsFromInputs();
            if (m_poolDesc == nullptr)
                m_poolDesc = m_factory->CreatePoolDescriptor(PoolingDescriptor::PoolKind::Average, m_windowWidth, m_windowHeight, m_horizontalSubsample, m_verticalSubsample, 0, 0);
        }
    };

    template class AveragePoolingNode<float>; 
    template class AveragePoolingNode<double>;    

    // Implements batch normalization technique as described in:
    // Batch Normalization: Accelerating Deep Network Training by Reducing Internal Covariate Shift [S. Ioffe, C. Szegedy]
    // http://arxiv.org/abs/1502.03167
    // REVIEW alexeyk: is this a right header for this node? It's not really limited to only convolutional nodes.
    template<class ElemType>
    class BatchNormalizationNode : public ComputationNode<ElemType>, public NumInputs<3>
    {
        typedef ComputationNode<ElemType> Base; UsingComputationNodeMembersBoilerplate;
        static const std::wstring TypeName() { return L"BatchNormalization"; }
    public:
        BatchNormalizationNode(DEVICEID_TYPE deviceId, const wstring & name) :
            Base(deviceId, name), m_spatial(false), m_expAvgFactor(0)
        {
        }

        BatchNormalizationNode(DEVICEID_TYPE deviceId, const wstring & name, bool spatial, double expAvgFactor) :
            Base(deviceId, name), m_spatial(spatial), m_expAvgFactor(expAvgFactor)
        {
        }

        void SaveToFile(File& fstream) const override
        {
            Base::SaveToFile(fstream);
            fstream << m_version.VerWrittenCur() << m_version.VerReadableCur();

            fstream << m_spatial;
            fstream << m_expAvgFactor;
        }

        void LoadFromFile(File& fstream, size_t modelVersion) override
        {
            Base::LoadFromFile(fstream, modelVersion);

            // Read and check version.
            // REVIEW alexeyk: extract version checking so it can be re-used in other places.
            int32_t verWritten;
            int32_t verReadable;
            fstream >> verWritten >> verReadable;

            if (verReadable > verWritten)
                RuntimeError("Corrupt model file.");
            if (verWritten < m_version.VerWeCanReadBack())
                RuntimeError("Model is too old.");
            if (verReadable > m_version.VerWrittenCur())
                RuntimeError("Model is too new.");

            fstream >> m_spatial;
            fstream >> m_expAvgFactor;
        }

        void CopyTo(ComputationNodeBasePtr nodeP, const std::wstring& newName, const CopyNodeFlags flags) const override
        {
            Base::CopyTo(nodeP, newName, flags);
            if (flags & CopyNodeFlags::copyNodeValue)
            {
                auto node = dynamic_pointer_cast<BatchNormalizationNode<ElemType>>(nodeP);
                assert(node != nullptr);

                node->m_expAvgFactor = m_expAvgFactor;
            }
        }

        void ComputeInputPartial(const size_t inputIndex, const FrameRange & frameRange) override
        {
        }

        void EvaluateThisNode(const FrameRange & frameRange) override
        {
            Matrix<ElemType> sliceInputValue = Inputs(0)->ValueSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));

            const Matrix<ElemType>& scale = Inputs(1)->FunctionValues();
            const Matrix<ElemType>& bias = Inputs(1)->FunctionValues();
            assert(scale.GetNumRows() == bias.GetNumRows());
            assert(scale.GetNumCols() == bias.GetNumCols());

            Matrix<ElemType> sliceOutputValue = ValueSlice(frameRange/*TODO: delete this:*/.Check_t(GetNumParallelSequences(), m_pMBLayout));

            size_t batchSize = m_pMBLayout->GetNumParallelSequences();
            m_inT->setN(batchSize);
            assert(m_convEng != nullptr);
#if NANCHECK
            sliceInputValue.HasNan("BatchNormalization-input");
#endif
            m_convEng->NormalizeBatch(*m_inT, sliceInputValue, *m_scaleBiasT, scale, bias, m_spatial, m_expAvgFactor, sliceOutputValue);
#if NANCHECK
            sliceOutputValue.HasNan("BatchNormalization");
#endif
        }

        void Validate(bool isFinalValidationPass) override
        {
            Base::Validate(isFinalValidationPass);

            InferMBLayoutFromInputsForStandardCase();
            InferImageDimsFromInputs();

            Resize(m_outputImageLayout.width * m_outputImageLayout.height * m_outputImageLayout.channels, Inputs(0)->GetNumCols());
        }

        void InferImageDimsFromInputs() override
        {
            InferImageDimsFromInput(0);

            if (m_factory == nullptr)
                m_factory = ConvolutionEngineFactory<ElemType>::Create(m_deviceId);
            if (m_convEng == nullptr)
                m_convEng = m_factory->CreateConvEngine(0);
            if (m_inT == nullptr)
            {
                m_inT = m_factory->CreateTensor(m_outputImageLayout.width, m_outputImageLayout.height, m_outputImageLayout.channels, 1);
            }
            if (m_scaleBiasT == nullptr)
            {
                if (m_spatial)
                    m_scaleBiasT = m_factory->CreateTensor(1, 1, m_outputImageLayout.channels, 1);
                else
                    m_scaleBiasT = m_factory->CreateTensor(m_outputImageLayout.width, m_outputImageLayout.height, m_outputImageLayout.channels, 1);
            }
        }

    private:
        struct VersionInfo
        {
            int32_t VerWrittenCur() const     { return 0x00010001; } // Initial
            int32_t VerReadableCur() const    { return 0x00010001; }
            int32_t VerWeCanReadBack() const  { return 0x00010001; }
        };
        VersionInfo m_version;

    private:
        std::unique_ptr<ConvolutionEngineFactory<ElemType>> m_factory;
        std::unique_ptr<ConvolutionEngine<ElemType>> m_convEng;
        std::unique_ptr<ConvolutionTensor4D> m_inT;
        std::unique_ptr<ConvolutionTensor4D> m_scaleBiasT;

        bool m_spatial;
        double m_expAvgFactor;
    };

    template class BatchNormalizationNode<float>; 
    template class BatchNormalizationNode<double>;    

}}}
