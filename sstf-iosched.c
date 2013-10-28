/* sstf-iosched.c
 * OSU - CS 411
 * Assignment 2
 * Group 14
 * Autohrs: hovekame, marickb, zubriske
 * Last Modified: October 27, 2013 
 */

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/init.h>

// Structure used to data references
struct sstf_data {
        struct list_head queue;
        sector_t head_pos;
};

// sstf_balance  - sorts the given element to its correct location
static void sstf_balance(struct sstf_data * sd) {
        struct request * rnext, * rprev;
        sector_t next, prev, pos;
	// check inputs
        if(list_empty(&sd->queue))
                return;
	// getting adject values 
        rnext = list_entry(sd->queue.next, struct request, queuelist);
        next = rnext->sector;
        
	rprev = list_entry(sd->queue.prev, struct request, queuelist);
        prev = rprev->sector;
        
	pos = sd->head_pos;
	//Sort occurs within while loop, until correct position found
        while(1) {
                //Upper edge
                if(pos > prev && next < prev)
                        break;
                //Lower edge
                if(pos < next && prev > next)
                        break;
                //Correct SPOT :-)
                if(pos < next && pos > prev)
                        break;
                if(pos > next) { 
			// Mov up 
                        list_move(&sd->queue,&rnext->queuelist);
                        rprev = rnext;
                        prev = next;
                        rnext = list_entry(sd->queue.next, struct request,
                                           queuelist);
                        next = rnext->sector;
                } else {
			// Move down
                        list_move_tail(&sd->queue,&rprev->queuelist);
                        rnext = rprev;
                        next = prev;
                        rprev = list_entry(sd->queue.prev, struct request,
                                          queuelist);
                        prev = rprev->sector;
                }
        }
}

// get_distance - returns absolute distance between current and given request
static int get_distance(struct sstf_data * sd, struct request * rq) {
        if(rq->sector < sd->head_pos)
                return sd->head_pos - rq->sector;
        else
                return rq->sector - sd->head_pos;
}

// noop_merged_requests - deletes the merged request
static void noop_merged_requests(struct request_queue *q, struct request *rq,
                                 struct request *next) {
        list_del_init(&next->queuelist);
}

// sstf_dispatch - gets shortest distance, removes from que and sends request 
static int sstf_dispatch(struct request_queue *q, int force) {
        // ensure real input
	if (q == NULL) return 0;
	struct sstf_data *nd = q->elevator->elevator_data;
	// ensure valid list
        if (!list_empty(&nd->queue)) {
                struct request * nextrq, * prevrq, * rq;  

                nextrq = list_entry(nd->queue.next, struct request, queuelist);
                prevrq = list_entry(nd->queue.prev, struct request, queuelist);

                if(get_distance(nd, nextrq) < get_distance(nd, prevrq))
                        rq = nextrq;
                else
                        rq = prevrq;
		//remove request
                list_del_init(&rq->queuelist);
                nd->head_pos = rq->sector + rq->nr_sectors - 1;
                elv_dispatch_sort(q, rq);
                //update queue head position
                sstf_balance(nd);
		//Tell kernel what is being read/written
                if(rq_data_dir(rq) == 0)
                        printk(KERN_INFO "[SSTF] dsp READ %ld\n",(long)rq->sector);
                else
                        printk(KERN_INFO "[SSTF] dsp WRITE %ld\n",(long)rq->sector);
                return 1;
        }
        return 0;
}

// sstf_add_request - check variables find correct postion and then add 
static void sstf_add_request(struct request_queue *q, struct request *rq) {
	// ensure valid inputs
	if (q == NULL || rq == NULL) 
		return;
        // variable definitions
	struct sstf_data *sd = q->elevator->elevator_data;
        struct request * rnext, * rprev;
        sector_t next, prev, pos;
	// base case if list is empty
        if(list_empty(&sd->queue))  {
                list_add(&rq->queuelist,&sd->queue);
                return;
        }
	// getting values for variables
        rnext = list_entry(sd->queue.next, struct request, queuelist);
        next = rnext->sector;
        
	rprev = list_entry(sd->queue.prev, struct request, queuelist);
        prev = rprev->sector;
        
	pos = rq->sector;
	// Get correct position to add request
        while(1) {
                //Upper edge
                if(pos > prev && next < prev)
                        break;
                //Lower edge
                if(pos < next && prev > next)
                        break;
                //correct position
                if(pos < next && pos > prev)
                        break;
                if(pos > next) {
			// Moving on Up (in the morning)
                        rprev = rnext;
                        prev = next;
                        rnext = list_entry(sd->queue.next, struct request,
                                           queuelist);
                        next = rnext->sector;
                } else {
			// down, down, down and the flames go higher
                        rnext = rprev;
                        next = prev;
                        rprev = list_entry(sd->queue.prev, struct request,
                                          queuelist);
                        prev = rprev->sector;
                }
        }
	// now add at the correct position
        __list_add(&rq->queuelist, &rprev->queuelist, &rnext->queuelist);
        printk(KERN_INFO "[SSTF] add %ld",(long) rq->sector);
}

// noop_queue_empty - return count of queue 
static int noop_queue_empty(struct request_queue *q) {
	if ( q == NULL) 
		return 0;	
	// get data 
        struct sstf_data *nd = q->elevator->elevator_data;
        return list_empty(&nd->queue);
}

// noop_former_request - returns previous reuest from the list
static struct request *noop_former_request(struct request_queue *q, 
		struct request *rq) {
	if (q == NULL || rq == NULL) 
		return 0;
	// get variable and return nessicary value 
	struct sstf_data *nd = q->elevator->elevator_data;
        if (rq->queuelist.prev == &nd->queue)
                return NULL;
        return list_entry(rq->queuelist.prev, struct request, queuelist);
}

// noop_latter_request - returns next request from the list
static struct request *noop_latter_request(struct request_queue *q,
		struct request *rq) {
	if (q == NULL || rq == NULL) 
		return 0;
	// get variable and return nessicary value
        struct sstf_data *nd = q->elevator->elevator_data;
        if (rq->queuelist.next == &nd->queue)
                return NULL;
        return list_entry(rq->queuelist.next, struct request, queuelist);
}

// noop_init_que - Setup data structure 
static void *noop_init_queue(struct request_queue *q) {
	if (q == NULL) 
		return;
	// create our data structure
        struct sstf_data *nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
	// check Kmalloc return value
        if (!nd)
                return NULL;
	// Initialize list poistion and head value and return data object
        INIT_LIST_HEAD(&nd->queue);
        nd->head_pos = 0;
        return nd;
}

// noop_exit_queue - delete elevator 
static void noop_exit_queue(elevator_t *e) {
	if (e == NULL)
		return;
	// get data to clear then free it
        struct sstf_data *nd = e->elevator_data;

        BUG_ON(!list_empty(&nd->queue));
        kfree(nd);
}

// Delecration of operations for elevator_type
static struct elevator_type elevator_noop = {
        .ops = {
                .elevator_merge_req_fn          = noop_merged_requests,
                .elevator_dispatch_fn           = sstf_dispatch,
                .elevator_add_req_fn            = sstf_add_request,
                .elevator_queue_empty_fn        = noop_queue_empty,
                .elevator_former_req_fn         = noop_former_request,
                .elevator_latter_req_fn         = noop_latter_request,
                .elevator_init_fn               = noop_init_queue,
                .elevator_exit_fn               = noop_exit_queue,
  },
        .elevator_name = "noop",
        .elevator_owner = THIS_MODULE,
};

// noop_init - returns elv_register value
static int __init noop_init(void)
{
        return elv_register(&elevator_noop);
}

// noop_exit - unregisters elevator
static void __exit noop_exit(void)
{
        elv_unregister(&elevator_noop);
}

module_init(noop_init);
module_exit(noop_exit);


MODULE_AUTHOR("Group 14 - OSU CS411 F13");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSTF IO scheduler ");

/* Example code we found is located at: 
 * http://code.google.com/p/cs411g16/source/browse/branches/project4/
 * 	block/sstf-iosched.c?r=100
 * https://github.com/ryleyherrington/linux_kernel_411/blob/master/
 * 	sstf-io/sstf-iosched.c
 * http://searchcode.com/codesearch/view/24999107
 */
