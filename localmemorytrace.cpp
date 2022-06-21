/*========================== begin_copyright_notice ============================
Copyright (C) 2018-2021 Intel Corporation

SPDX-License-Identifier: MIT
============================= end_copyright_notice ===========================*/

/*!
 * @file Implementation of the Memorytrace tool
 */

#include <fstream>

#include "memorytrace.h"
#include "gtpin_tool_utils.h"

using namespace gtpin;
using namespace std;

/* ============================================================================================= */
// Configuration
/* ============================================================================================= */
Knob<int>  knobMaxTraceBufferInMB("max_buffer_mb", 3072, "memorytrace - the max allowed size of the trace buffer per kernel in MB\n");
Knob<int>  knobPhase("phase", 0, "tracing tool - processing phase\n { 1 - pre-processing, 2 - processing - trace gathering} ");

/* ============================================================================================= */
// BblMemAccessInfo implementation
/* ============================================================================================= */
BblMemAccessInfo& BblMemAccessInfo::Build(const IGtKernelInstrument& kernelInstrument, const IGtBbl& bbl)
{
    const IGtCfg&       cfg             = kernelInstrument.Cfg();
    uint32_t            addrPayloadSize = 0;
    uint32_t            headerSize      = 0;
    const IGtGenModel&  genModel        = kernelInstrument.Kernel().GenModel();

    _memInstructions.clear();
    for (auto insPtr : bbl.Instructions())
    {
        const IGtIns& ins = *insPtr;
        if (ins.IsSendMessage())
        {
            DcSendMsg msg = DcSendMsg::Decode(ins.GetGedIns());
            if ((msg.IsValid() || ins.IsEot()) && msg.IsSlm())
            {
                addrPayloadSize += (msg.AddrPayloadLength() * genModel.GrfRegSize());   // Accumulate SEND address payloads
                headerSize = MemTraceRecordHeader::AlignedSize(genModel);               // Add one trace record header per BBL
                _memInstructions.emplace_back(MemIns{ins.Id(), cfg.GetInstructionOffset(ins), std::move(msg)});
            }
        }
    }
    _recordSize = addrPayloadSize + headerSize;
    return *this;
}

/* ============================================================================================= */
// KernelMemAccessInfo implementation
/* ============================================================================================= */
KernelMemAccessInfo& KernelMemAccessInfo::Build(const IGtKernelInstrument& kernelInstrument)
{
    const IGtCfg&  cfg = kernelInstrument.Cfg();

    _memAccessMap.clear();
    _maxRecordSize = 0;
    for (auto bblPtr : cfg.Bbls())
    {
        const IGtBbl& bbl = *bblPtr;
        BblMemAccessInfo bblMemAccessInfo(kernelInstrument, bbl);
        if (!bblMemAccessInfo.IsEmpty())
        {
            _maxRecordSize = std::max(_maxRecordSize, bblMemAccessInfo.RecordSize());
            _memAccessMap.emplace(bbl.Id(), std::move(bblMemAccessInfo));
        }
    }
    return *this;
}

const BblMemAccessInfo* KernelMemAccessInfo::GetBblInfo(BblId bblId) const 
{
    auto it = _memAccessMap.find(bblId);
    return (it == _memAccessMap.end()) ? nullptr: &(it->second);
}

/* ============================================================================================= */
// MemTraceDispatch implementation
/* ============================================================================================= */
bool MemTraceDispatch::ReadTrace(const GtProfileTrace& traceAccessor, const IGtProfileBuffer& profileBuffer)
{
    uint32_t traceSize = traceAccessor.Size(profileBuffer);
    _rawTrace.resize(traceSize);
    _isTrimmed = traceAccessor.IsTruncated(profileBuffer);
    return traceAccessor.Read(profileBuffer, _rawTrace.data(), 0, traceSize);
}

bool MemTraceDispatch::IsEmpty() const
{
    return _rawTrace.size() < sizeof(MemTraceRecordHeader);
}

/* ============================================================================================= */
// MemTraceKernel implementation
/* ============================================================================================= */
MemTraceKernel::MemTraceKernel(const IGtKernelInstrument& kernelInstrument)
{
    const IGtKernel& kernel = kernelInstrument.Kernel();
    const IGtCfg&    cfg    = kernelInstrument.Cfg();

    _name       = GlueString(kernel.Name());
    _extName    = ExtendedKernelName(kernel);
    _platform   = kernel.GpuPlatform();
    _genId      = kernel.GenModel().Id();
    _asmText    = CfgAsmText(cfg);

    // Build static information about memory accesses in the kernel
    _memAccessInfo.Build(kernelInstrument);

    // Initialize trace accessor. The trace capacity is expected to be computed during the preprocessing phase.
    uint64_t traceCapacity =  MemoryTracePreProcessor::Instance()->TraceSize(_extName);
    if (traceCapacity == 0)
    {
        traceCapacity = UINT32_MAX; // Unknown trace capacity
    }
    else
    {
        traceCapacity += 0x2000; // Add some space to account for possible fluctuation of trace sizes between phases
        if (traceCapacity > UINT32_MAX)
        {
            GTPIN_WARNING("MEMORYTRACE: Too big trace capacity required for kernel " + _name);
            traceCapacity = UINT32_MAX;
        }
    }
    traceCapacity = std::min(uint64_t(knobMaxTraceBufferInMB) * 0x100000, traceCapacity);
    uint32_t maxRecordSize = _memAccessInfo.MaxRecordSize();
    _traceAccessor = GtProfileTrace((uint32_t)traceCapacity, maxRecordSize);
    _traceAccessor.Allocate(kernelInstrument.ProfileBufferAllocator());
}

MemTraceDispatch& MemTraceKernel::AddMemTrace(IGtKernelDispatch& kernelDispatch)
{
    // Create a new MemTraceDispatch object and store the entire trace within this object
    _traces.emplace_back(kernelDispatch);
    MemTraceDispatch& memTraceDispatch  = _traces.back();
    if (!memTraceDispatch.ReadTrace(_traceAccessor, *kernelDispatch.GetProfileBuffer()))
    {
        GTPIN_ERROR_MSG("MEMORYTRACE: Failed to read profile buffer for kernel " + _name);
    }
    return memTraceDispatch;
}

void MemTraceKernel::DumpAsm() const
{
    DumpKernelAsmText(_name, _asmText);
}

/* ============================================================================================= */
// MemTrace implementation
/* ============================================================================================= */
MemTrace* MemTrace::Instance()
{
    static MemTrace instance;
    return &instance;
}

bool MemTrace::Register(IGtCore* gtpinCore)
{
    if (!gtpinCore->RegisterTool(*this))
    {
        GTPIN_ERROR_MSG(string("MEMORYTRACE: Failed registration with GTPin core. ") +
                        "GTPin API version = " + to_string(gtpinCore->ApiVersion()) +
                        "Tool API version = " + to_string(ApiVersion()));
        return false;
    }
    _gtpinCore = gtpinCore;
    return true;
}

void MemTrace::OnKernelBuild(IGtKernelInstrument& instrumentor)
{
    const IGtKernel& kernel = instrumentor.Kernel();

    // Create new KernelData object and add it to the data base
    auto ret = _kernels.emplace(kernel.Id(), instrumentor);
    if (ret.second)
    {
        MemTraceKernel& memTraceKernel = (*ret.first).second;
        if (!memTraceKernel.IsEnabled())
        {
            GTPIN_WARNING("MEMORYTRACE: The trace won't be generated for kernel " + memTraceKernel.Name());
            return;
        }

        const IGtCfg&   cfg   = instrumentor.Cfg();
        IGtVregFactory& vregs = instrumentor.Coder().VregFactory();

        // Initialize virtual registers
        _addrReg    = vregs.MakeMsgAddrScratch();
        _dataReg    = vregs.MakeMsgDataScratch(VREG_TYPE_HWORD);
        _offsetReg  = vregs.Make(VREG_TYPE_DWORD);

        // Instrument basic blocks
        for (auto bblPtr : cfg.Bbls())
        {
            InstrumentBbl(instrumentor, *bblPtr, memTraceKernel);
        }
    }
}

void MemTrace::OnKernelRun(IGtKernelDispatch& dispatcher)
{
    bool isProfileEnabled = false;

    const IGtKernel& kernel = dispatcher.Kernel();
    GtKernelExecDesc execDesc; dispatcher.GetExecDescriptor(execDesc);
    if (IsKernelExecProfileEnabled(execDesc, kernel.GpuPlatform()))
    {
        auto it = _kernels.find(kernel.Id());
        if (it != _kernels.end())
        {
            const MemTraceKernel&  memTraceKernel = it->second;
            if (memTraceKernel.IsEnabled())
            {
                IGtProfileBuffer*     buffer        = dispatcher.CreateProfileBuffer(); GTPIN_ASSERT(buffer);
                const GtProfileTrace& traceAccessor = memTraceKernel.TraceAccessor();
                if (traceAccessor.Initialize(*buffer))
                {
                    isProfileEnabled = true;
                }
                else
                {
                    GTPIN_ERROR_MSG("MEMORYTRACE: Failed to write into memory buffer for kernel " + string(kernel.Name()));
                }
            }
        }
    }
    dispatcher.SetProfilingMode(isProfileEnabled);
}

void MemTrace::OnKernelComplete(IGtKernelDispatch& dispatcher)
{
    if (!dispatcher.IsProfilingEnabled())
    {
        return; // Do nothing with unprofiled kernel dispatches
    }

    const IGtKernel& kernel = dispatcher.Kernel();
    auto it = _kernels.find(kernel.Id());
    if (it != _kernels.end())
    {
        // Read the trace from the profile buffer
        MemTraceKernel&  memTraceKernel = it->second;
        memTraceKernel.AddMemTrace(dispatcher);
    }
}

bool MemTrace::InstrumentBbl(IGtKernelInstrument& instrumentor, const IGtBbl& bbl, const MemTraceKernel& memTraceKernel)
{
    const KernelMemAccessInfo&  kernelMemAccessInfo = memTraceKernel.GetMemAccessInfo();
    const BblMemAccessInfo*     bblInfoPtr          = kernelMemAccessInfo.GetBblInfo(bbl.Id());
    if ((bblInfoPtr == nullptr) || bblInfoPtr->IsEmpty())
    {
        return false; // The basic block does not contain any memory instruction of interest
    }

    const IGtGenCoder&      coder         = instrumentor.Coder();
    const IGtCfg&           cfg           = instrumentor.Cfg();
    const BblMemAccessInfo& bblInfo       = *bblInfoPtr;

    // Generate code that allocates space for the new record in the trace and stores the trace record header.
    // Insert this procedure before the first memory access in the basic block.
    GtGenProcedure headerProc;
    const IGtIns&  firstMemIns = cfg.GetInstruction(bblInfo.MemInstructions().front().id);
    StoreRecordHeader(headerProc, coder, bbl, memTraceKernel, bblInfo.RecordSize());
    instrumentor.InstrumentInstruction(firstMemIns, GtIpoint::Before(), headerProc);

    // Generate code that stores address payload registers of SEND instructions in the basic block
    for (const auto& insInfo : bblInfo.MemInstructions())
    {
        GtGenProcedure      proc;
        const IGtIns&       ins                 = cfg.GetInstruction(insInfo.id);
        const DcSendMsg&    msg                 = insInfo.msg;
        GtRegNum            src0RegNum          = msg.Src0();
        GtRegNum            src1RegNum          = msg.Src1();
        uint32_t            addrPayloadLength   = msg.AddrPayloadLength();
        uint32_t            src0Length          = msg.Src0Length();

        // If size of the SRC0 register range is greater or equal to the address payload length, the SRC0 range contains
        // the entire address payload.
        // Otherwise the address payload is split between SRC0 and SRC1 register ranges
        if (src0Length >= addrPayloadLength)
        {
            StoreRegRange(proc, coder, src0RegNum, addrPayloadLength);
        }
        else
        {
            StoreRegRange(proc, coder, src0RegNum, src0Length);
            if (src1RegNum.IsValid())
            {
                StoreRegRange(proc, coder, src1RegNum, addrPayloadLength - src0Length);
            }
        }
        instrumentor.InstrumentInstruction(ins, GtIpoint::Before(), proc);
    }

    return true;
}

void MemTrace::StoreRecordHeader(GtGenProcedure& proc, const IGtGenCoder& coder, const IGtBbl& bbl,
                                 const MemTraceKernel& memTraceKernel, uint32_t recordSize)
{
    /// @return Subregister of _dataReg intended to hold the value of the MemTraceRecordHeader field whose offset is specified
    auto fieldReg = [&](uint32_t fieldOffset) -> GtReg { return GtReg(_dataReg, sizeof(uint32_t), fieldOffset / sizeof(uint32_t)); };

    GtReg idFieldReg    = fieldReg(offsetof(MemTraceRecordHeader, bblId));
    GtReg ceFieldReg    = fieldReg(offsetof(MemTraceRecordHeader, ce));
    GtReg dmFieldReg    = fieldReg(offsetof(MemTraceRecordHeader, dm));
    GtReg cr0FieldReg   = fieldReg(offsetof(MemTraceRecordHeader, cr0));
    GtReg flag0FieldReg = fieldReg(offsetof(MemTraceRecordHeader, flag0));
    GtReg flag1FieldReg = fieldReg(offsetof(MemTraceRecordHeader, flag1));

    IGtInsFactory&  insF = coder.InstructionFactory();
    GtPredicate     predicate(FlagReg(0));

    // Set values of MemTraceRecordHeader fields in _dataReg 
    proc += insF.MakeShl(idFieldReg, StateReg(0), 16);                      // idFieldReg[16:31] = sr0.0
    proc += insF.MakeAdd(idFieldReg, idFieldReg, GtImmU32(bbl.Id()));       // idFieldReg[0:15]  = bbl.Id()
    proc += insF.MakeMov(ceFieldReg, ChannelEnableReg());                   // ceFieldReg        = ChannelEnableReg()
    proc += insF.MakeMov(dmFieldReg, DispatchMaskReg());                    // dmFieldReg        = DispatchMaskReg()
    proc += insF.MakeMov(flag0FieldReg, FlagReg(0));                        // flag0FieldReg     = FlagReg(0)
    proc += insF.MakeMov(flag1FieldReg, FlagReg(1));                        // flag1FieldReg     = FlagReg(1)
    if (bbl.IsEntry()) { proc += insF.MakeMov(cr0FieldReg, ControlReg()); } // cr0FieldReg       = ControlReg()()

    // Allocate new record in the trace.
    // Set _offsetReg = offset of the allocated record in the profile buffer, _addrReg = address of the allocated record
    memTraceKernel.TraceAccessor().ComputeNewRecordOffset(coder, proc, recordSize, _offsetReg);
    coder.ComputeAddress(proc, _addrReg, _offsetReg);

    // Zero _offsetReg if the trace buffer is overflowed (predicate == true)
    proc += insF.MakeMov(_offsetReg, 0).SetPredicate(predicate);

    //if (!predicate) { STORE buffer[_offsetReg] = _dataReg;  _offsetReg += aligned-header-size}
    uint32_t alignedHeaderSize = MemTraceRecordHeader::AlignedSize(memTraceKernel.GenModel());
    coder.StoreMemBlock(proc, _addrReg, _dataReg, alignedHeaderSize, !predicate);
    proc += insF.MakeAdd(_offsetReg, _offsetReg, alignedHeaderSize).SetPredicate(!predicate);

    if (!proc.empty()) { proc.front()->AppendAnnotation(__func__); }
}

void MemTrace::StoreRegRange(GtGenProcedure& proc, const IGtGenCoder& coder, uint32_t firstRegNum, uint32_t numRegs)
{
    if (numRegs == 0) { return; }

    IGtInsFactory&  insF        = coder.InstructionFactory();
    uint32_t        grfRegSize  = insF.GenModel().GrfRegSize();
    GtReg           flagReg     = FlagReg(0);
    GtPredicate     predicate(flagReg);

    // predicate = (_offsetReg != 0) = trace is not overflowed
    proc += insF.MakeCmp(GED_COND_MODIFIER_nz, flagReg, _offsetReg, 0, {16});

    do
    {
        uint32_t numBlockRegs = std::min(AlignPower2Down(numRegs), uint32_t(4));
        uint32_t blockSize    = numBlockRegs * grfRegSize;

        // Store payload in range [firstRegNum, firstRegNum + numBlockRegs]
        coder.ComputeAddress(proc, _addrReg, _offsetReg);
        coder.StoreMemBlock(proc, _addrReg, GrfReg(firstRegNum, 0, grfRegSize), blockSize, predicate);
        proc += insF.MakeAdd(_offsetReg, _offsetReg, blockSize).SetPredicate(predicate);

        numRegs     -= numBlockRegs;
        firstRegNum += numBlockRegs;
    } while (numRegs != 0);

    if (!proc.empty()) { proc.front()->AppendAnnotation(__func__); }
}

void MemTrace::OnFini()
{
    MemTrace& me = *Instance();
    for (auto& ref : me._kernels)
    {
        const MemTraceKernel&  memTraceKernel = ref.second;
        MemoryTracePostProcessor(*me._gtpinCore, memTraceKernel)();
        memTraceKernel.DumpAsm();
    }
}

/* ============================================================================================= */
// MemoryTracePreProcessor implementation
/* ============================================================================================= */
const char* MemoryTracePreProcessor::_kernelPreProcessFileName   = "memorytrace_pre_process.txt";
const char* MemoryTracePreProcessor::_dispatchPreProcessFileName = "memorytrace_pre_process_dispatch.txt";

MemoryTracePreProcessor::MemoryTracePreProcessor()
{
    if (knobPhase == 2)
    {
        // Read the data collected during the preprocessing phase
        std::ifstream is(_kernelPreProcessFileName);
        GTPIN_ASSERT_MSG(is, string("File ") + _kernelPreProcessFileName + " does not exist. The trace won't be generated");
        is >> _kernelCounters;
    }
}

MemoryTracePreProcessor* MemoryTracePreProcessor::Instance()
{
    static MemoryTracePreProcessor instance;
    return &instance;
}

void MemoryTracePreProcessor::OnFini()
{
    MemoryTracePreProcessor&  tool = *Instance();
    tool.DumpKernelProfiles(_kernelPreProcessFileName);
    tool.DumpDispatchProfiles(_dispatchPreProcessFileName);
}

uint64_t MemoryTracePreProcessor::TraceSize(const string& extKernelName) const
{
    auto it = _kernelCounters.find(extKernelName);
    return ((it == _kernelCounters.end()) ? 0 : it->second.weight);
}

uint32_t MemoryTracePreProcessor::GetBblWeight(IGtKernelInstrument& kernelInstrument, const IGtBbl& bbl) const
{
    // For the memorytrace tool, the weight of the BBL is the trace record size in this BBL
    return BblMemAccessInfo(kernelInstrument, bbl).RecordSize();
}

void MemoryTracePreProcessor::AggregateDispatchCounters(KernelWeightCounters& kc, KernelWeightCounters dc) const
{
    kc.weight = std::max(kc.weight, dc.weight);
    kc.freq += dc.freq;
}

/* ============================================================================================= */
// MemoryTracePostProcessor implementation
/* ============================================================================================= */
const char* MemoryTracePostProcessor::_traceFileName = "memorytrace_compressed.bin";

MemoryTracePostProcessor::MemoryTracePostProcessor(const IGtCore& gtpinCore, const MemTraceKernel& memTraceKernel) :
    _kernel(&memTraceKernel), _memAccessInfo(&memTraceKernel.GetMemAccessInfo()),
    _kernelDir(JoinPath(string(gtpinCore.ProfileDir()), NormalizeFilename(memTraceKernel.Name()))) {}

bool MemoryTracePostProcessor::operator()()
{
    if (!MakeDirectory(_kernelDir))
    {
        GTPIN_WARNING("MEMORYTRACE: Could not create directory " + _kernelDir);
        return false;
    }

    // Process traces recorded in kernel dispatches
    for (const MemTraceDispatch& trace : _kernel->GetTraces())
    {
        if (!trace.IsEmpty())
        {
            if (trace.IsTrimmed())
            {
                GTPIN_WARNING("MEMORYTRACE: Detected trace buffer overflow in kernel " + _kernel->Name());
            }

            string subdir   = trace.KernelExecDesc().ToString(_kernel->Platform(), ExecDescFileNameFormat());
            string dir      = MakeSubDirectory(_kernelDir, subdir);
            string filePath = JoinPath(dir, _traceFileName);

            ofstream fs(filePath, std::ios::binary);
            if (!fs)
            {
                GTPIN_WARNING("MEMORYTRACE: Could not create file " + filePath);
                continue;
            }
            StoreTrace(trace, fs);
        }
    }
    return true;
}

void MemoryTracePostProcessor::StoreTrace(const MemTraceDispatch& trace, std::ofstream& fs)
{
    const uint8_t* traceData         = trace.Data();
    uint32_t       traceSize         = trace.Size();
    uint32_t       alignedHeaderSize = MemTraceRecordHeader::AlignedSize(_kernel->GenModel());

    // Associate trace records with threads - populate _threadTraceRecords array
    const GtStateRegAccessor& sra = _kernel->GenModel().StateRegAccessor();
    uint32_t maxThreads = _kernel->GenModel().MaxThreads(); // Max number of HW threads
    _threadTraceRecords.clear();
    _threadTraceRecords.resize(maxThreads);

    uint32_t numProfiledThreads = 0; // Number of profiled (active) threads
    for (uint32_t recordOffset = 0; recordOffset + sizeof(MemTraceRecordHeader) <= traceSize;)
    {
        // Retrive thread ID and BBL ID from the record header
        const MemTraceRecordHeader* header = (const MemTraceRecordHeader*)(traceData + recordOffset);
        uint32_t tid   = sra.GetGlobalTid(header->sr0);
        uint32_t bblId = header->bblId;
        const BblMemAccessInfo* bblInfoPtr = _memAccessInfo->GetBblInfo(bblId); GTPIN_ASSERT(bblInfoPtr != nullptr);
        uint32_t recordSize = bblInfoPtr->RecordSize();
        if (recordOffset + recordSize > traceSize)
        {
            break; // end of trace
        }

        // Add a new trace record reference to _threadTraceRecords
        if (_threadTraceRecords[tid].empty()) { ++numProfiledThreads; } // Increment thread count on the first relevant record
        _threadTraceRecords[tid].emplace_back(TraceRecord{header, recordSize});

        recordOffset += recordSize;
    }

    StoreMemAccessInfo(fs);         // Store static information about memory accesses in the kernel
    Store(numProfiledThreads, fs);  // Store the number of profiled threads

    // Store per-thread traces
    for (uint32_t tid = 0; tid < maxThreads; tid++)
    {
        const TraceRecordList& traceRecordList = _threadTraceRecords[tid];
        if (traceRecordList.empty()) { continue; }

        StoreGlobalTid(tid, fs);    // Store Global Thread Identifier

        uint32_t numRecords = (uint32_t)traceRecordList.size();
        Store(numRecords, fs);      // Store #records collected in the thread

        // Store trace records
        for (const auto& record : traceRecordList)
        {
            const auto& header   = *(record.header);
            uint32_t    bblId    = header.bblId;
            uint32_t    execMask = header.ce & header.dm;

            Store(bblId, fs);       // Store BBL ID
            Store(execMask, fs);    // Store dynamic execution mask

            // Store address paylads
            if (record.size > alignedHeaderSize)
            {
                fs.write((const char*)(record.header) + alignedHeaderSize, record.size - alignedHeaderSize);
            }
        }
    }
}

void MemoryTracePostProcessor::StoreMemAccessInfo(std::ofstream& fs)
{
    // Store static information about memory accesses in BBLs
    uint32_t numBbls = _memAccessInfo->NumMemBbls();
    Store(numBbls, fs);                                 // Store the number of BBLs that access memory

    for (const auto& entry : _memAccessInfo->GetMemAccessMap())
    {
        uint32_t                bblId               = entry.first;
        const BblMemAccessInfo& bblInfo             = entry.second;
        uint32_t                numMemInstructions  = (uint32_t)bblInfo.MemInstructions().size();

        Store(bblId, fs);                               // Store BBL ID
        Store(numMemInstructions, fs);                  // Store the number of memory instructions in BBL
        for (const auto& memIns : bblInfo.MemInstructions())
        {
            PackedMemIns packedMemIns(memIns);
            Store(packedMemIns, fs);                    // Store the memory instruction descriptor
        }
    }
}

void MemoryTracePostProcessor::StoreGlobalTid(uint32_t gtid, std::ofstream& fs)
{
    const GtStateRegAccessor& sra = _kernel->GenModel().StateRegAccessor();
    uint32_t sr0 = sra.SetGlobalTid(0, gtid);

    auto storeSr0Field = [&](const ScatteredBitFieldU32& sbf)
    {
        uint32_t val = (sbf.IsEmpty() ? UINT32_MAX : sbf.GetValue(sr0));
        Store(val, fs);
    };

    storeSr0Field(sra.SliceIdField());
    storeSr0Field(sra.DualSubSliceIdField());
    storeSr0Field(sra.SubSliceIdField());
    storeSr0Field(sra.EuIdField());
    storeSr0Field(sra.ThreadSlotField());
}

MemoryTracePostProcessor::PackedMemIns::PackedMemIns(const MemIns& memIns)
{
    offset              = memIns.offset;
    isWrite             = memIns.msg.IsWrite();
    isScatter           = memIns.msg.IsScatter();
    isBTS               = memIns.msg.IsBts();
    isSLM               = memIns.msg.IsSlm();
    isScratch           = memIns.msg.IsScratch();
    isAtomic            = memIns.msg.IsAtomic();
    addressWidth        = (memIns.msg.IsA64() ? 1 : 0);
    simdWidth           = (memIns.msg.SimdWidth() == 16);
    bti                 = memIns.msg.Bti();
    elementSize         = memIns.msg.ElementSize();
    numElements         = memIns.msg.NumElements();
    addrPayloadLength   = memIns.msg.AddrPayloadLength();
    dataPort            = (memIns.msg.IsDp1() ? 1 : 0);
    isEOT               = memIns.msg.IsEot();
    isMedia             = memIns.msg.IsMedia();
    res                 = 0;
    execSize            = memIns.msg.ExecSize();
    channelOffset       = memIns.msg.ChannelOffset();
}

/* ============================================================================================= */
// GTPin_Entry
/* ============================================================================================= */
EXPORT_C_FUNC void GTPin_Entry(int argc, const char *argv[])
{
    IGtCore* gtpinCore = GTPin_GetCore();

    ConfigureGTPin(argc, argv);
    if (knobPhase == 1)
    {
        MemoryTracePreProcessor::Instance()->Register(GTPin_GetCore());
        atexit(MemoryTracePreProcessor::OnFini);
    }
    else
    {
        GTPIN_ASSERT_MSG((knobPhase == 2), "MEMORYTRACE: Invalid phase value. Should be 1 or 2, provided " + std::to_string(knobPhase));
        MemTrace::Instance()->Register(gtpinCore);
        atexit(MemTrace::OnFini);
    }
}


