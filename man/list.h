#ifndef _DCLIST_H
#define _DCLIST_H

/*
 * Doubly linked, circular, linked list.
 * This is so I don't have to keep writing the same logic on every
 * project. Also, this type of linked list is very general making most
 * use cases trivial to implement.
 *
 * This implementation is also basically ripped straight from
 * include/linux/list.h but strips away all the kernel ~magic~
 */

#include <stddef.h>

#ifndef offsetof
# warning "offsetof() not defined, rolling own..."
# define offsetof(type, member) ((size_t) &((type *) 0)->member)
#endif

struct list_node {
	struct list_node *next, *prev;
};

/* Compile time initalizers */
#define LIST_NODE_INIT(node) { &(node), &(node) }
#define LIST_NODE(name) \
	struct list_node name = LIST_NODE_INIT(name)

/*
 * init_list_node - initialize a list node
 * @l:	node to initalize
 */
static inline void init_list_node(struct list_node *l)
{
	l->next = l;
	l->prev = l;
}

/*
 * list_empty - check if list is empty
 * @head:	head of list
 */
static inline int list_empty(const struct list_node *head)
{
	return head->next == head;
}

/*
 * list_is_singular - check if list has exactly one entry
 * @head:	head of list
 */
static inline int list_is_singluar(const struct list_node *head)
{
	return !list_empty(head) && (head->next == head->prev);
}

/*
 * list_is_tail - check if node is last entry in list
 * @l:		node to check
 * @head:	head of list
 */
static inline int list_is_tail(const struct list_node *l,
				const struct list_node *head)
{
	return head->prev == l;
}

/*
 * list_is_first - check if node is first entry in list
 * @l:		node to check
 * @head:	head of list
 */
static inline int list_is_first(const struct list_node *l,
				const struct list_node *head)
{
	return l->prev == head;
}

static inline void list_replace_node(struct list_node *new, struct list_node *old)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
}

/*
 * __list_del_node - internal function to delete a node from a list
 * @l_prev:	pointer to previous node in list
 * @l_next:	pointer to next node in the list
 */
static inline void __list_del_node(struct list_node *l_prev, struct list_node *l_next)
{
	l_prev->next = l_next;
	l_next->prev = l_prev;
}

/*
 * list_del_node - delete a node from a list
 * @l:	node to delete
 */
static inline void list_del_node(struct list_node *l)
{
	__list_del_node(l->prev, l->next);
}

/*
 * list_del_init - delete a node from a list, then reinitialize it
 * @l: node to delete and reinit
 */
static inline void list_del_init(struct list_node *l)
{
	__list_del_node(l->prev, l->next);
	init_list_node(l);
}

/*
 * __list_add_post - internal function to add a node to a list after a node
 * @new:	new node to add
 * @curr:	node that will be before the new node
 * @next:	node that will be after the new node
 */
static inline void __list_add_post(struct list_node *new, struct list_node *prev,
					struct list_node *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

/*
 * list_add_post - add a node to a list after a node
 * @nw:		node to add into list
 * @curr	node that will become before the new node
 */
static inline void list_add_post(struct list_node *nw, struct list_node *curr)
{
	__list_add_post(nw, curr, curr->next);
}

/*
 * __list_add_prev - internal function to add a node to a list before a node
 * @new:		new node to add
 * @curr:	node that will be after the new node
 * @prev:	node that will be before the new node
 */
static inline void __list_add_prev(struct list_node *new, struct list_node *curr,
					struct list_node *prev)
{
	new->prev = prev;
	new->next = curr;
	prev->next = new;
	curr->prev = new;
}

/*
 * list_add_prev - add a node to a list before a node
 * @l_new:	node to add into list
 * @r:		node what will be after the new node
 */
static inline void list_add_prev(struct list_node *l_new, struct list_node *r)
{
	__list_add_prev(l_new, r, r->prev);
}

static inline void __list_del(struct list_node *prev, struct list_node *next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void __list_del_entry(struct list_node *entry)
{
	__list_del(entry->prev, entry->next);
}

static inline void list_del(struct list_node *entry)
{
	__list_del_entry(entry);
}

static inline void list_move(struct list_node *l, struct list_node *head)
{
	__list_del_entry(l);
	list_add_post(l, head);
}

static inline void list_replace(struct list_node *old, struct list_node *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
}

static inline void list_swap(struct list_node *entry1, struct list_node *entry2)
{
	struct list_node *p = entry2->prev;

	list_del(entry2);
	list_replace(entry1, entry2);
	if (p == entry1)
		p = entry2;
	list_add_post(entry1, p);
}

#define list_entry(ptr, type, member)				\
	((type *) ((void *) (ptr) - offsetof(type, member)))	\

#define list_first_entry(pos, type, member) \
	list_entry((pos)->next, type, member)

#define list_last_entry(ptr, type, member)	\
	list_entry((ptr)->prev, type, member)

#define list_entry_is_head(pos, head, member) \
	(&pos->member == (head))

#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_prev_entry(pos, member)	\
	list_entry((pos)->member.prev, typeof(*(pos)), member)

#define list_for_each(cursor, head) \
	for ((cursor) = (head)->next; (cursor) != (head); (cursor) = (cursor)->next)

#define list_for_each_from(cursor, head) \
	for (; (cursor) != (head); (cursor) = (cursor)->next)

#define list_for_each_rev(cursor, head) \
	for ((cursor) = (head)->prev; (cursor) != (head); (cursor) = (cursor)->prev)

#define list_for_each_safe(cursor, l, head) 				\
	for (cursor = (head)->next, l = cursor->next; cursor != (head); \
			cursor = l, n = cursor->next)

#define list_for_each_entry(pos, head, member) \
	for (pos = list_first_entry(head, typeof(*pos), member);	\
		!list_entry_is_head(pos, head, member);			\
		pos = list_next_entry(pos, member))

#define list_for_each_entry_reverse(pos, head, member)		\
	for (pos = list_last_entry(head, typeof(*pos), member);	\
		!list_entry_is_head(pos, head, member);		\
		pos = list_prev_entry(pos, member))

#define list_for_each_entry_from(from, head, member)	\
	for (; !list_entry_is_head(from, head, member);	\
		from = list_next_entry(pos, member))

#define list_for_each_entry_safe(pos, n, head, member) 			\
	for (pos = list_first_entry(head, typeof(*pos), member), 	\
		n = list_next_entry(pos, member);			\
		!list_entry_is_head(pos, head, member);			\
		pos = n, n = list_next_entry(n, member))

#define list_for_each_entry_continue(pos, head, member)		\
	for (pos = list_next_entry(pos, member);		\
		!list_entry_is_head(pos, head, member);		\
		pos = list_next_entry(pos, member))

#endif /* _DCLIST_H */
