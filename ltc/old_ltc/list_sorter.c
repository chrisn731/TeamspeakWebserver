#include "list.h"
#include "list_sort.h"

/*
 * Merge sort implementaion of a linked list
 *
 * All credit to linux/lib/list_sort.h
 */
__attribute__((nonnull(1,2,3)))
static struct list_node *merge(list_cmp_func cmp, struct list_node *a,
				struct list_node *b)
{
	struct list_node *head, **tail = &head;

	for (;;) {
		if (cmp(a, b) <= 0) {
			*tail = a;
			tail = &a->next;
			a = a->next;
			if (!a) {
				*tail = b;
				break;
			}
		} else {
			*tail = b;
			tail = &b->next;
			b = b->next;
			if (!b) {
				*tail = a;
				break;
			}
		}
	}
	return head;
}

__attribute__((nonnull(1,2,3,4)))
static void merge_final(list_cmp_func cmp, struct list_node *head,
				struct list_node *a, struct list_node *b)
{
	struct list_node *tail = head;

	for (;;) {
		if (cmp(a, b) <= 0) {
			tail->next = a;
			a->prev = tail;
			tail = a;
			a = a->next;
			if (!a)
				break;
		} else {
			tail->next = b;
			b->prev = tail;
			tail = b;
			b = b->next;
			if (!b) {
				b = a;
				break;
			}
		}
	}

	tail->next = b;
	do {
		b->prev = tail;
		tail = b;
		b = b->next;
	} while (b);

	tail->next = head;
	head->prev = tail;

}

__attribute__((nonnull(1,2)))
void list_sort(struct list_node *head, list_cmp_func cmp)
{
	struct list_node *list = head->next, *pending = NULL;
	size_t count = 0;

	if (list == head->prev)
		return;

	head->prev->next = NULL;
	do {
		size_t bits;
		struct list_node **tail = &pending;

		for (bits = count; bits & 1; bits >>= 1)
			tail = &(*tail)->prev;
		if (bits) {
			struct list_node *a = *tail, *b = a->prev;
			a = merge(cmp, b, a);
			a->prev = b->prev;
			*tail = a;
		}
		list->prev = pending;
		pending = list;
		list = list->next;
		pending->next = NULL;
		count++;
	} while (list);

	list = pending;
	pending = pending->prev;
	for (;;) {
		struct list_node *next = pending->prev;

		if (!next)
			break;
		list = merge(cmp, pending, list);
		pending = next;
	}
	merge_final(cmp, head, pending, list);
}
