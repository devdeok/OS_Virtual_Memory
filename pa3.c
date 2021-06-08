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
	 * currnet process에 page table 만들어야 됨
	 */

	// Hiereachical Page Tables
	int pd_index = vpn / NR_PTES_PER_PAGE; //page directory : outer
    int pte_index = vpn % NR_PTES_PER_PAGE; //page table entity : inner
    int pfn_index; // physical frame number
	
    for(pfn_index = 0; pfn_index < NR_PAGEFRAMES; pfn_index++){
		if(mapcounts[pfn_index]==0) // linke된 곳이 없음
			break;
    }

   /* 메모리가 이미 찼을 경우 -1 return
	* vm.c에서 __alloc_page를 통해 처리됨
	* 메모리가 가득차지 않을 경우 __translate를 통해 
	* vpn을 pfn으로 translate함
	*/
    if(pfn_index >= NR_PAGEFRAMES) //page frame의 개수를 넘어가면 -1 
		return -1;
	
    if(current->pagetable.outer_ptes[pd_index]==NULL){ //pd is invalid
		current->pagetable.outer_ptes[pd_index] = malloc(sizeof(struct pte_directory));
    }
    current->pagetable.outer_ptes[pd_index]->ptes[pte_index].valid = true;

	if(rw == 1) current->pagetable.outer_ptes[pd_index]->ptes[pte_index].writable = false;
	else current->pagetable.outer_ptes[pd_index]->ptes[pte_index].writable = true;

    current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn = pfn_index;
	
	mapcounts[pfn_index]++; //page frame이 할당되었으므로 비어있는 index에 link된 개수 업데이트

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

	if(current->pagetable.outer_ptes[pd_index]->ptes[pte_index].private==1 &&
	   mapcounts[ptbr->outer_ptes[pd_index]->ptes[pte_index].pfn]>1){//하나의 pfn에 2개이상 할당
		current->pagetable.outer_ptes[pd_index]->ptes[pte_index].writable=1;// 쓰기모드로 변경
		mapcounts[ptbr->outer_ptes[pd_index]->ptes[pte_index].pfn]--;//해당 pfn 1줄이고
		alloc_page(vpn,rw);//새로운 pfn 배정
		return true;
	}	

	if(current->pagetable.outer_ptes[pd_index]->ptes[pte_index].private==1 &&
	   mapcounts[ptbr->outer_ptes[pd_index]->ptes[pte_index].pfn]==1){//하나의 pfn에 1개만 할당됨
		current->pagetable.outer_ptes[pd_index]->ptes[pte_index].writable = 1;//쓰기 모드로 변경
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
	 * pid가 있는 process가 있는 경우 그 process로 switch
	 * @current process는 @processes list에 put, 그리고 @current process는
	 * requested process로 replace
	 * next process가 @processes로부터 unlinked되고 @ptbr이 올바르게 설정되어있는지 확인
	*/ 
	list_for_each_entry(temp,&processes,list){
		if(temp->pid == pid){ // pid가 있음
			list_add_tail(&current->list,&processes);
			list_del_init(&temp->list);
			current = temp;
			ptbr = &(temp->pagetable);
			return;
		}//if (입력된 pid와 process의 pid가 같음)
	}//list_for_each_entry

	/** 
	 * pid가 있는 process가 없는 경우 @currnet에서 process를 fork
 	 * forked child process가 parent's page table(current)과 동일한 page table entry를
	 * 가져야 함을 의미한다.
	 * copy-on-write를 구현하기 위해서는 pte의 writable bit와 
	 * shared page의 mapcount를 manipulate해야함(wirtable를 꺼두어야 함)
	 * 일부 useful information을 저장하기 위해서는 pte->private를 사용할 수 있음
	 */
	child = malloc(sizeof(struct process)); // fork할 process
	child->pid = pid;

	for(int i=0;i<NR_PTES_PER_PAGE;i++){
		if(current->pagetable.outer_ptes[i] != NULL){ //currnet pd is valid
			child->pagetable.outer_ptes[i] = malloc(sizeof(struct pte_directory));

			for(int j=0;j<NR_PTES_PER_PAGE;j++){
				if(current->pagetable.outer_ptes[i]->ptes[j].valid){
					child->pagetable.outer_ptes[i]->ptes[j].writable = false;//CoW
					current->pagetable.outer_ptes[i]->ptes[j].writable = false;//CoW
					
					child->pagetable.outer_ptes[i]->ptes[j].valid = 
						current->pagetable.outer_ptes[i]->ptes[j].valid;

					child->pagetable.outer_ptes[i]->ptes[j].pfn = 
						current->pagetable.outer_ptes[i]->ptes[j].pfn;

					child->pagetable.outer_ptes[i]->ptes[j].private = 1;
					current->pagetable.outer_ptes[i]->ptes[j].private = 1;

					mapcounts[child->pagetable.outer_ptes[i]->ptes[j].pfn]++;
				}
			}//for i
		}//if (pd is valid)
	}//for j


	list_add_tail(&current->list,&processes);
	current = child;
	ptbr = &(child->pagetable);
}

