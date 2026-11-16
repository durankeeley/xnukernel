/*
 * xcross: Apple's iig generates one OSAction subclass per method carrying a TYPE() attribute (OSAction_<Class>_<Method>).
 * The stock kernel therefore defines OSAction_IOUserClient_KernelCompletion and exports its metaclass symbols; kexts reference it.
 * The stand-in iig (toolchain/bin/iig) does not generate these classes, so define the one the kernel is expected to export here.
 */
#include <IOKit/IOUserServer.h>
#include <IOKit/IOUserClient.h>
#include <DriverKit/OSAction.h>

class OSAction_IOUserClient_KernelCompletion : public OSAction
{
	OSDeclareDefaultStructors(OSAction_IOUserClient_KernelCompletion);
	typedef OSAction super;
};

OSDefineMetaClassAndStructors(OSAction_IOUserClient_KernelCompletion, OSAction);

/* xcross debugging aid (osfmk/kern/xcross_dump.c): which kext contains this address? */
#include <libkern/c++/OSKext.h>
extern "C" int
xcross_kext_lookup(uint64_t addr, char *name, int len, uint64_t *off)
{
	OSPtr<OSKext> k = OSKext::lookupKextWithAddress((vm_address_t)addr);
	if (!k) {
		return 0;
	}
	const char *n = k->getIdentifierCString();
	strlcpy(name, n ? n : "?", len);
	*off = 0;   /* kmod_info is private; callers print the offset from the kernel text base instead */
	return 1;
}
