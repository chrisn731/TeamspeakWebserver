#ifndef _LIST_SORT_H
#define _LIST_SORT_H

typedef int __attribute__((nonnull(1,2)))
	(*list_cmp_func)(const struct list_node *, const struct list_node *);

__attribute__((nonnull(1,2)))
	void list_sort(struct list_node *head, list_cmp_func cmp);
#endif
