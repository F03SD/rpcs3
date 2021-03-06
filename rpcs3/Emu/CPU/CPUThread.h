#pragma once
#include "Emu/Memory/MemoryBlock.h"
#include "Emu/CPU/CPUDecoder.h"
#include "Utilities/SMutex.h"

struct reservation_struct
{
	SMutex mutex; // mutex for updating reservation_owner and data
	volatile u32 owner; // id of thread that got reservation
	volatile u32 addr;
	volatile u32 size;
	volatile u32 data32;
	volatile u64 data64;
	// atm, PPU can't break SPU MFC reservation correctly

	__forceinline void clear()
	{
		owner = 0;
	}
};

extern reservation_struct reservation;

enum CPUThreadType :unsigned char
{
	CPU_THREAD_PPU,
	CPU_THREAD_SPU,
	CPU_THREAD_RAW_SPU,
	CPU_THREAD_ARMv7,
};

enum CPUThreadStatus
{
	CPUThread_Ready,
	CPUThread_Running,
	CPUThread_Paused,
	CPUThread_Stopped,
	CPUThread_Sleeping,
	CPUThread_Break,
	CPUThread_Step,
};

class CPUThread : public ThreadBase
{
protected:
	u32 m_status;
	u32 m_error;
	u32 m_id;
	u64 m_prio;
	u64 m_offset;
	CPUThreadType m_type;
	bool m_joinable;
	bool m_joining;
	bool m_is_step;

	u64 m_stack_addr;
	u64 m_stack_size;
	u64 m_stack_point;

	u64 m_exit_status;

	CPUDecoder* m_dec;

public:
	virtual void InitRegs()=0;

	virtual void InitStack()=0;
	virtual void CloseStack();

	u64 GetStackAddr() const { return m_stack_addr; }
	u64 GetStackSize() const { return m_stack_size; }
	virtual u64 GetFreeStackSize() const=0;

	void SetStackAddr(u64 stack_addr) { m_stack_addr = stack_addr; }
	void SetStackSize(u64 stack_size) { m_stack_size = stack_size; }

	virtual void SetArg(const uint pos, const u64 arg) = 0;

	void SetId(const u32 id);
	void SetName(const std::string& name);
	void SetPrio(const u64 prio) { m_prio = prio; }
	void SetOffset(const u64 offset) { m_offset = offset; }
	void SetExitStatus(const u64 status) { m_exit_status = status; }

	u64 GetOffset() const { return m_offset; }
	u64 GetExitStatus() const { return m_exit_status; }
	u64 GetPrio() const { return m_prio; }

	std::string GetName() const { return NamedThreadBase::GetThreadName(); }
	wxString GetFName() const
	{
		return 
			wxString::Format("%s[%d] Thread%s", 
				GetTypeString().wx_str(),
				m_id,
				wxString(GetName().empty() ? "" : wxString::Format(" (%s)", + wxString(GetName()).wx_str())).wx_str()
			);
	}

	static wxString CPUThreadTypeToString(CPUThreadType type)
	{
		switch(type)
		{
		case CPU_THREAD_PPU: return "PPU";
		case CPU_THREAD_SPU: return "SPU";
		case CPU_THREAD_RAW_SPU: return "RawSPU";
		case CPU_THREAD_ARMv7: return "ARMv7";
		}

		return "Unknown";
	}

	wxString GetTypeString() const { return CPUThreadTypeToString(m_type); }

	virtual std::string GetThreadName() const
	{
		wxString temp = (GetFName() + wxString::Format("[0x%08llx]", PC));
		return std::string(temp.mb_str());
	}

public:
	u64 entry;
	u64 PC;
	u64 nPC;
	u64 cycle;
	bool m_is_branch;

protected:
	CPUThread(CPUThreadType type);

public:
	virtual ~CPUThread();

	u32 m_wait_thread_id;

	wxCriticalSection m_cs_sync;
	bool m_sync_wait;
	void Wait(bool wait);
	void Wait(const CPUThread& thr);
	bool Sync();

	template<typename T>
	void WaitFor(T func)
	{
		while(func(ThreadStatus()))
		{
			Sleep(1);
		}
	}

	int ThreadStatus();

	void NextPc(u8 instr_size);
	void SetBranch(const u64 pc, bool record_branch = false);
	void SetPc(const u64 pc);
	void SetEntry(const u64 entry);

	void SetError(const u32 error);

	static wxArrayString ErrorToString(const u32 error);
	wxArrayString ErrorToString() { return ErrorToString(m_error); }

	bool IsOk()		const { return m_error == 0; }
	bool IsRunning()	const { return m_status == Running; }
	bool IsPaused()		const { return m_status == Paused; }
	bool IsStopped()	const { return m_status == Stopped; }

	bool IsJoinable() const { return m_joinable; }
	bool IsJoining()  const { return m_joining; }
	void SetJoinable(bool joinable) { m_joinable = joinable; }
	void SetJoining(bool joining) { m_joining = joining; }

	u32 GetError() const { return m_error; }
	u32 GetId() const { return m_id; }
	CPUThreadType GetType()	const { return m_type; }

	void Reset();
	void Close();
	void Run();
	void Pause();
	void Resume();
	void Stop();

	virtual void AddArgv(const wxString& arg) {}

	virtual wxString RegsToString() = 0;
	virtual wxString ReadRegString(wxString reg) = 0;
	virtual bool WriteRegString(wxString reg, wxString value) = 0;

	virtual void Exec();
	void ExecOnce();

	struct CallStackItem
	{
		u64 pc;
		u64 branch_pc;
	};

	Stack<CallStackItem> m_call_stack;

	wxString CallStackToString()
	{
		wxString ret = "Call Stack:\n==========\n";

		for(uint i=0; i<m_call_stack.GetCount(); ++i)
		{
			ret += wxString::Format("0x%llx -> 0x%llx\n", m_call_stack[i].pc, m_call_stack[i].branch_pc);
		}

		return ret;
	}

	void CallStackBranch(u64 pc)
	{
		for(int i=m_call_stack.GetCount() - 1; i >= 0; --i)
		{
			if(CallStackGetNextPC(m_call_stack[i].pc) == pc)
			{
				m_call_stack.RemoveAt(i, m_call_stack.GetCount() - i);
				return;
			}
		}

		CallStackItem new_item;

		new_item.branch_pc = pc;
		new_item.pc = PC;

		m_call_stack.Push(new_item);
	}

	virtual u64 CallStackGetNextPC(u64 pc)
	{
		return pc + 4;
	}

	s64 ExecAsCallback(u64 pc, bool wait, u64 a1 = 0, u64 a2 = 0, u64 a3 = 0, u64 a4 = 0);

protected:
	virtual void DoReset()=0;
	virtual void DoRun()=0;
	virtual void DoPause()=0;
	virtual void DoResume()=0;
	virtual void DoStop()=0;

protected:
	virtual void Step() {}
	virtual void Task();
};

CPUThread* GetCurrentCPUThread();
