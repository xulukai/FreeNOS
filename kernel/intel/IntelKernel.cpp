/*
 * Copyright (C) 2009 Niek Linnenbank
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <FreeNOS/API.h>
#include <FreeNOS/ProcessManager.h>
#include <Macros.h>
#include <List.h>
#include <ListIterator.h>
#include <Vector.h>
#include <String.h>
#include <BootImage.h>
#include <UserProcess.h>
#include "IntelKernel.h"
#include "IntelCPU.h"
#include "IntelInterrupt.h"
#include "IntelMemory.h"
#include "Multiboot.h"

/** Interrupt handlers. */
Vector<List<InterruptHook *> *> interrupts(256);

void executeInterrupt(CPUState state)
{
    /* Fetch the list of interrupt hooks (for this vector). */
    List<InterruptHook *> *lst = interrupts[state.vector];

    /* Does at least one handler exist? */
    if (!lst)
        return;

    /* Execute them all. */
    for (ListIterator<InterruptHook *> i(lst); i.hasCurrent(); i++)
    {
        i.current()->handler(&state, i.current()->param);
    }
}

IntelKernel::IntelKernel(IntelMemory *memory, ProcessManager *proc)
    : Kernel(memory, proc), Singleton<IntelKernel>(this)
{
    /* ICW1: Initialize PIC's (Edge triggered, Cascade) */
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    
    /* ICW2: Remap IRQ's to interrupts 32-47. */
    outb(PIC1_DATA, PIC_IRQ_BASE);
    outb(PIC2_DATA, PIC_IRQ_BASE + 8);

    /* ICW3: PIC2 is connected to PIC1 via IRQ2. */
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    /* ICW4: 8086 mode, fully nested, not buffered, no implicit EOI. */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* OCW1: Disable all IRQ's for now. */
    outb(PIC1_DATA, 0xff);
    outb(PIC2_DATA, 0xff);

    /* Let the i8253 timer run continuously (square wave). */
    outb(PIT_CMD, 0x36);
    outb(PIT_CHAN0, PIT_DIVISOR & 0xff);
    outb(PIT_CHAN0, PIT_DIVISOR >> 8);
    
    /* Make sure to enable PIC2 and the i8253. */
    enableIRQ(2, true);
    enableIRQ(0, true);

    // Clear interrupts table
    interrupts.fill(ZERO);

    /* Setup exception handlers. */
    for (int i = 0; i < 17; i++)
    {
        hookInterrupt(i, exception, 0);
    }
    /* Setup IRQ handlers. */
    for (int i = 17; i < 256; i++)
    {
        /* Trap gate. */
        if (i == 0x90)
            hookInterrupt(0x90, trap, 0);

        /* Hardware Interrupt. */
        else
            hookInterrupt(i, interrupt, 0);
    }
    /* Install PIT (i8253) IRQ handler. */
    hookInterrupt(IRQ(0), clocktick, 0);

    /* Initialize TSS Segment. */
    gdt[USER_TSS].limitLow    = sizeof(TSS) + (0xfff / 8);
    gdt[USER_TSS].baseLow     = ((Address) &kernelTss) & 0xffff;
    gdt[USER_TSS].baseMid     = (((Address) &kernelTss) >> 16) & 0xff;
    gdt[USER_TSS].type        = 9;
    gdt[USER_TSS].privilege  = 0;
    gdt[USER_TSS].present     = 1;
    gdt[USER_TSS].limitHigh   = 0;
    gdt[USER_TSS].granularity = 8;
    gdt[USER_TSS].baseHigh    = (((Address) &kernelTss) >> 24) & 0xff;

    /* Let TSS point to I/O bitmap page. */
    kernelTss.bitmap = PAGESIZE << 16;

    /* Load Task State Register. */
    ltr(USER_TSS_SEL);
}

void IntelKernel::hookInterrupt(int vec, InterruptHandler h, ulong p)
{
    InterruptHook hook(h, p);

    /* Insert into interrupts; create List if neccessary. */
    if (!interrupts[vec])
    {
        interrupts.insert(vec, new List<InterruptHook *>());
    }
    /* Just append it. */
    if (!interrupts[vec]->contains(&hook))
    {
	interrupts[vec]->append(new InterruptHook(h, p));
    }
}

void IntelKernel::enableIRQ(uint irq, bool enabled)
{
    if (enabled)
    {
    	if (irq < 8)
    	    outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << irq));
    	else
    	    outb(PIC2_DATA, inb(PIC2_DATA) & ~(1 << (irq - 8)));
    }
    else
    {
    	if (irq < 8)
    	    outb(PIC1_DATA, inb(PIC1_DATA) | (1 << irq));
    	else
    	    outb(PIC2_DATA, inb(PIC2_DATA) | (1 << (irq - 8)));
    }
}

void IntelKernel::exception(CPUState *state, ulong param)
{
    ProcessManager *procs = Kernel::instance->getProcessManager();

    assert(procs->current() != ZERO);
    procs->remove(procs->current());
    procs->schedule();
}

void IntelKernel::interrupt(CPUState *state, ulong param)
{
    /* End of Interrupt to slave. */
    if (IRQ(state->vector) >= 8)
    {
        outb(PIC2_CMD, PIC_EOI);
    }
    /* End of Interrupt to master. */
    outb(PIC1_CMD, PIC_EOI);
}

void IntelKernel::trap(CPUState *state, ulong param)
{
    state->eax = Kernel::instance->invokeAPI(
        (APINumber) state->eax,
                    state->ecx,
                    state->ebx,
                    state->edx,
                    state->esi,
                    state->edi
    );
}

void IntelKernel::clocktick(CPUState *state, ulong param)
{
    IntelKernel *kernel = Singleton<IntelKernel>::instance;

    /* Reschedule. */
    kernel->getProcessManager()->schedule();
}

bool IntelKernel::loadBootImage()
{
    MultibootModule *mod;
    BootImage *image;

    /* Startup boot modules. */
    for (Size n = 0; n < multibootInfo.modsCount; n++)
    {
        mod = &((MultibootModule *) multibootInfo.modsAddress)[n];
        String str = (char *) mod->string;

        /* Mark its memory used */
        for (Address a = mod->modStart; a < mod->modEnd; a += PAGESIZE)
        {
            m_memory->allocatePhysicalAddress(a);
        }

        /* Is this the BootImage? */
        if (str.match("*.img.gz"))
        {
            /* Map the BootImage into our address space. */
            image = (BootImage *) m_memory->map(mod->modStart);
                                  m_memory->map(mod->modStart + PAGESIZE);

            /* Verify this is a correct BootImage. */
            if (image->magic[0] == BOOTIMAGE_MAGIC0 &&
                image->magic[1] == BOOTIMAGE_MAGIC1 &&
                image->layoutRevision == BOOTIMAGE_REVISION)
            {
                m_bootImageAddress = mod->modStart;
                m_bootImageSize    = mod->modEnd - mod->modStart;

                /* Loop BootPrograms. */
                for (Size i = 0; i < image->symbolTableCount; i++)
                    loadBootProcess(image, mod->modStart, i);
            }
            return true;
        }
    }
    return false;
}

void IntelKernel::loadBootProcess(BootImage *image, Address imagePAddr, Size index)
{
    Address imageVAddr = (Address) image, args;
    BootSymbol *program;
    BootSegment *segment;
    Process *proc;
    
    // Point to the program and segments table
    program = &((BootSymbol *) (imageVAddr + image->symbolTableOffset))[index];
    segment = &((BootSegment *) (imageVAddr + image->segmentsTableOffset))[program->segmentsOffset];

    // Ignore non-BootProgram entries
    if (program->type != BootProgram)
        return;

    // Create process
    proc = m_procs->create(program->entry);
    proc->setState(Process::Ready);
                    
    // Loop program segments
    for (Size i = 0; i < program->segmentsCount; i++)
    {
        // Map program segment into it's virtual memory
        for (Size j = 0; j < segment[i].size; j += PAGESIZE)
        {

            m_memory->map(proc,
                          imagePAddr + segment[i].offset + j,
                          segment[i].virtualAddress + j,
                          Memory::Present | Memory::User | Memory::Readable | Memory::Writable);
        }
    }
    // Map and copy program arguments
    args = m_memory->allocatePhysical(PAGESIZE);
    m_memory->map(proc, args, ARGV_ADDR, Memory::Present | Memory::User | Memory::Writable);
    MemoryBlock::copy( (char *) m_memory->map(args), program->name, ARGV_SIZE);

    NOTICE("loaded: " << program->name);
}
