/**********************************************************************
 * Copyright (c) 2020-2021
 *  Sang-Hoon Kim <sanghoonkim@ajou.ac.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "types.h"
#include "list_head.h"
#include "vm.h"

/**
 * Ready queue of the system
 */
extern struct list_head processes;

/**
 * Currently running process
 */
extern struct process *current;

/**
 * Page Table Base Register that MMU will walk through for address translation
 */
extern struct pagetable *ptbr;

/**
 * The number of mappings for each page frame. Can be used to determine how
 * many processes are using the page frames.
 */
extern unsigned int mapcounts[];


/**
 * alloc_page(@vpn, @rw)
 *
 * DESCRIPTION
 *   Allocate a page frame that is not allocated to any process, and map it
 *   to @vpn. When the system has multiple free pages, this function should
 *   allocate the page frame with the **smallest pfn**.
 *   You may construct the page table of the @current process. When the page
 *   is allocated with RW_WRITE flag, the page may be later accessed for writes.
 *   However, the pages populated with RW_READ only should not be accessed with
 *   RW_WRITE accesses. 
 *
 * RETURN
 *   Return allocated page frame number.
 *   Return -1 if all page frames are allocated.
 */
unsigned int alloc_page(unsigned int vpn, unsigned int rw){
	/** NR_PAGEFRAMES : 128
	 *  PTES_PER_PAGE_SHIFT : 4
	 *  NR_PTES_PER_PAGE : 1 << PTES_PER_PAGE_SHIFT(4) : 2^0->2^4(16)
	 *  RW_READ : 0x01
	 *  RW_WRITE : 0x02
	 *  r:1 / w:2 / rw:3
	 * 
	 * currnet process??? page table ???????????? ???
	 */

	// Hiereachical Page Tables
	int pd_index = vpn / NR_PTES_PER_PAGE; //page directory : outer
    int pte_index = vpn % NR_PTES_PER_PAGE; //page table entity : inner
    int pfn_index; // physical frame number
	
    for(pfn_index = 0; pfn_index < NR_PAGEFRAMES; pfn_index++){
		if(mapcounts[pfn_index]==0) // linke??? ?????? ??????
			break;
    }

   /* ???????????? ?????? ?????? ?????? -1 return
	* vm.c?????? __alloc_page??? ?????? ?????????
	* ???????????? ???????????? ?????? ?????? __translate??? ?????? 
	* vpn??? pfn?????? translate???
	*/
    if(pfn_index >= NR_PAGEFRAMES) //page frame??? ????????? ???????????? -1 
		return -1;
	
    if(current->pagetable.outer_ptes[pd_index]==NULL){ //pd is invalid
		current->pagetable.outer_ptes[pd_index] = malloc(sizeof(struct pte_directory));
    }
    current->pagetable.outer_ptes[pd_index]->ptes[pte_index].valid = true;

	if(rw == 1) current->pagetable.outer_ptes[pd_index]->ptes[pte_index].writable = false;
	else current->pagetable.outer_ptes[pd_index]->ptes[pte_index].writable = true;

    current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn = pfn_index;
	
	mapcounts[pfn_index]++; //page frame??? ????????????????????? ???????????? index??? link??? ?????? ????????????

    return pfn_index;
}

/**
 * free_page(@vpn)
 *
 * DESCRIPTION
 *   Deallocate the page from the current processor. Make sure that the fields
 *   for the corresponding PTE (valid, writable, pfn) is set @false or 0.
 *   Also, consider carefully for the case when a page is shared by two processes,
 *   and one process is to free the page.
 */
void free_page(unsigned int vpn){
	int pd_index = vpn / NR_PTES_PER_PAGE;
    int pte_index = vpn % NR_PTES_PER_PAGE;

	mapcounts[current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn]--;

	current->pagetable.outer_ptes[pd_index]->ptes[pte_index].valid = false;
	current->pagetable.outer_ptes[pd_index]->ptes[pte_index].writable = false;
	current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn = 0;
}


/**
 * handle_page_fault()
 *
 * DESCRIPTION
 *   Handle the page fault for accessing @vpn for @rw. This function is called
 *   by the framework when the __translate() for @vpn fails. This implies;
 *   0. page directory is invalid
 *   1. pte is invalid
 *   2. pte is not writable but @rw is for write
 *   This function should identify the situation, and do the copy-on-write if
 *   necessary.
 *
 * RETURN
 *   @true on successful fault handling
 *   @false otherwise
 */
bool handle_page_fault(unsigned int vpn, unsigned int rw){
	int pte_index= vpn % 16;
	int pd_index = vpn / 16;

	//page directory is invalid
	if(current->pagetable.outer_ptes[pd_index]==NULL){
		current->pagetable.outer_ptes[pd_index] = malloc(sizeof(struct pte_directory));
		current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn = alloc_page(vpn,rw);
		return true;
	}
	
	//pte is invalid
	if(current->pagetable.outer_ptes[pd_index]->ptes[pte_index].valid == false){
		current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn = alloc_page(vpn,rw);
		return true;
	}

	if(current->pagetable.outer_ptes[pd_index]->ptes[pte_index].private==true &&
	   mapcounts[ptbr->outer_ptes[pd_index]->ptes[pte_index].pfn]>1){//????????? pfn??? 2????????? ??????
		current->pagetable.outer_ptes[pd_index]->ptes[pte_index].writable=1;// ??????????????? ??????
		mapcounts[ptbr->outer_ptes[pd_index]->ptes[pte_index].pfn]--;//?????? pfn 1?????????
		current->pagetable.outer_ptes[pd_index]->ptes[pte_index].private=false;
		alloc_page(vpn,rw);//????????? pfn ??????
		return true;
	}	

	if(current->pagetable.outer_ptes[pd_index]->ptes[pte_index].private==true &&
	   mapcounts[ptbr->outer_ptes[pd_index]->ptes[pte_index].pfn]==1){//????????? pfn??? 1?????? ?????????
		current->pagetable.outer_ptes[pd_index]->ptes[pte_index].writable = 1;//?????? ????????? ??????
		current->pagetable.outer_ptes[pd_index]->ptes[pte_index].private=false;
		return true;
	}

	return false;
}


/**
 * switch_process()
 *
 * DESCRIPTION
 *   If there is a process with @pid in @processes, switch to the process.
 *   The @current process at the moment should be put into the @processes
 *   list, and @current should be replaced to the requested process.
 *   Make sure that the next process is unlinked from the @processes, and
 *   @ptbr is set properly.
 *
 *   If there is no process with @pid in the @processes list, fork a process
 *   from the @current. This implies the forked child process should have
 *   the identical page table entry 'values' to its parent's (i.e., @current)
 *   page table. 
 *   To implement the copy-on-write feature, you should manipulate the writable
 *   bit in PTE and mapcounts for shared pages. You may use pte->private for 
 *   storing some useful information :-)
 */
void switch_process(unsigned int pid){
	struct process *temp = NULL;
	struct process *child = NULL;

	// printf("pid : %d\n",pid);

	/** 
	 * pid??? ?????? process??? ?????? ?????? ??? process??? switch
	 * @current process??? @processes list??? put, ????????? @current process???
	 * requested process??? replace
	 * next process??? @processes????????? unlinked?????? @ptbr??? ???????????? ????????????????????? ??????
	*/ 
	list_for_each_entry(temp,&processes,list){
		if(temp->pid == pid){ // pid??? ??????
			list_add_tail(&current->list,&processes);
			list_del_init(&temp->list);
			current = temp;
			ptbr = &(temp->pagetable);
			return;
		}//if (????????? pid??? process??? pid??? ??????)
	}//list_for_each_entry

	/** 
	 * pid??? ?????? process??? ?????? ?????? @currnet?????? process??? fork
 	 * forked child process??? parent's page table(current)??? ????????? page table entry???
	 * ????????? ?????? ????????????.
	 * copy-on-write??? ???????????? ???????????? pte??? writable bit??? 
	 * shared page??? mapcount??? manipulate?????????(wirtable??? ???????????? ???)
	 * ?????? useful information??? ???????????? ???????????? pte->private??? ????????? ??? ??????
	 */
	child = malloc(sizeof(struct process)); // fork??? process
	child->pid = pid;

	for(int i=0;i<NR_PTES_PER_PAGE;i++){
		if(current->pagetable.outer_ptes[i] != NULL){ //currnet pd is valid
			child->pagetable.outer_ptes[i] = malloc(sizeof(struct pte_directory));

			for(int j=0;j<NR_PTES_PER_PAGE;j++){
				if(current->pagetable.outer_ptes[i]->ptes[j].valid){

					if(current->pagetable.outer_ptes[i]->ptes[j].writable==true)//???????????? ?????????
						current->pagetable.outer_ptes[i]->ptes[j].private = true;

					child->pagetable.outer_ptes[i]->ptes[j].writable = false;//CoW
					current->pagetable.outer_ptes[i]->ptes[j].writable = false;//CoW
					
					child->pagetable.outer_ptes[i]->ptes[j].valid = 
						current->pagetable.outer_ptes[i]->ptes[j].valid;

					child->pagetable.outer_ptes[i]->ptes[j].pfn = 
						current->pagetable.outer_ptes[i]->ptes[j].pfn;

					child->pagetable.outer_ptes[i]->ptes[j].private =
						current->pagetable.outer_ptes[i]->ptes[j].private;

					mapcounts[child->pagetable.outer_ptes[i]->ptes[j].pfn]++;
				}
			}//for i
		}//if (pd is valid)
	}//for j


	list_add_tail(&current->list,&processes);
	current = child;
	ptbr = &(child->pagetable);
}

