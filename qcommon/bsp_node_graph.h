/* Private BSP38 disk-node validation shared by the renderer and collision reader. */
#ifndef R1Q2_BSP_NODE_GRAPH_H
#define R1Q2_BSP_NODE_GRAPH_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
	BSP38_NODE_GRAPH_OK,
	BSP38_NODE_GRAPH_BAD_NODE,
	BSP38_NODE_GRAPH_BAD_LEAF,
	BSP38_NODE_GRAPH_SHARED_NODE,
	BSP38_NODE_GRAPH_CYCLE,
	BSP38_NODE_GRAPH_NOMEM,
	BSP38_NODE_GRAPH_TOO_DEEP
} bsp38_node_graph_status_t;

typedef struct
{
	int node;
	int side;
	int child;
} bsp38_node_graph_error_t;

typedef struct
{
	int node;
	int next_child;
} bsp38_node_graph_frame_t;

static int BSP38_NodeChildLayout (const byte *records, int node, int side,
	size_t node_stride, size_t children_offset)
{
	int32 disk_child;

	memcpy (&disk_child, records + (size_t)node * node_stride + children_offset + side * sizeof (disk_child),
		sizeof (disk_child));
	return LittleLong (disk_child);
}

static bsp38_node_graph_status_t BSP38_ValidateNodeGraphLayout (
	const byte *records, int num_nodes, int num_leafs,
	size_t node_stride, size_t children_offset,
	bsp38_node_graph_error_t *error)
{
	bsp38_node_graph_frame_t *stack = NULL;
	byte *state = NULL;
	bsp38_node_graph_status_t status = BSP38_NODE_GRAPH_OK;
	int i, side, child, depth, root;

	if (!error)
		return BSP38_NODE_GRAPH_NOMEM;
	error->node = error->side = error->child = -1;
	if (!records || num_nodes < 1 || num_nodes > MAX_MAP_NODES ||
		node_stride < 2 * sizeof (int32) ||
		children_offset > node_stride - 2 * sizeof (int32))
		return BSP38_NODE_GRAPH_BAD_NODE;
	if (num_leafs < 0 || num_leafs > MAX_MAP_LEAFS)
		return BSP38_NODE_GRAPH_BAD_LEAF;
	for (i = 0; i < num_nodes; i++)
	{
		for (side = 0; side < 2; side++)
		{
			child = BSP38_NodeChildLayout (records, i, side,
				node_stride, children_offset);
			if ((child >= 0 && child >= num_nodes) ||
				(child < 0 && child < -num_leafs))
			{
				error->node = i;
				error->side = side;
				error->child = child;
				return child >= 0 ? BSP38_NODE_GRAPH_BAD_NODE :
									BSP38_NODE_GRAPH_BAD_LEAF;
			}
		}
	}

	state = calloc ((size_t)num_nodes, 1);
	stack = malloc ((size_t)num_nodes * sizeof (*stack));
	if (!state || !stack)
	{
		status = BSP38_NODE_GRAPH_NOMEM;
		goto done;
	}
	for (i = 0; i < num_nodes; i++)
	{
		for (side = 0; side < 2; side++)
		{
			child = BSP38_NodeChildLayout (records, i, side,
				node_stride, children_offset);
			if (child < 0)
				continue;
			if (state[child])
			{
				error->node = i;
				error->side = side;
				error->child = child;
				status = BSP38_NODE_GRAPH_SHARED_NODE;
				goto done;
			}
			state[child] = 1;
		}
	}
	memset (state, 0, (size_t)num_nodes);

	for (root = 0; root < num_nodes; root++)
	{
		if (state[root])
			continue;
		state[root] = 1;
		stack[0].node = root;
		stack[0].next_child = 0;
		depth = 1;
		while (depth)
		{
			bsp38_node_graph_frame_t *frame = &stack[depth - 1];
			if (frame->next_child == 2)
			{
				state[frame->node] = 2;
				depth--;
				continue;
			}
			side = frame->next_child++;
			child = BSP38_NodeChildLayout (records, frame->node, side,
				node_stride, children_offset);
			if (child < 0)
				continue;
			if (state[child] == 1)
			{
				error->node = frame->node;
				error->side = side;
				error->child = child;
				status = BSP38_NODE_GRAPH_CYCLE;
				goto done;
			}
			if (state[child] == 0)
			{
				state[child] = 1;
				stack[depth].node = child;
				stack[depth].next_child = 0;
				depth++;
			}
		}
	}

done:
	free (stack);
	free (state);
	return status;
}

/* Optional QBSP admission ceiling for recursive consumers. Classic callers
 * keep the uncapped validator above and preserve their existing behavior. */
static bsp38_node_graph_status_t BSP38_ValidateNodeGraphLayoutLimit (
	const byte *records, int num_nodes, int num_leafs,
	size_t node_stride, size_t children_offset, int max_depth,
	bsp38_node_graph_error_t *error)
{
	bsp38_node_graph_status_t status;
	int *parents, i, side, child, node, depth;

	status = BSP38_ValidateNodeGraphLayout (records, num_nodes, num_leafs,
		node_stride, children_offset, error);
	if (status != BSP38_NODE_GRAPH_OK)
		return status;
	if (max_depth < 1)
		return BSP38_NODE_GRAPH_TOO_DEEP;
	parents = malloc ((size_t)num_nodes * sizeof (*parents));
	if (!parents)
		return BSP38_NODE_GRAPH_NOMEM;
	for (i = 0; i < num_nodes; i++)
		parents[i] = -1;
	for (i = 0; i < num_nodes; i++)
		for (side = 0; side < 2; side++)
		{
			child = BSP38_NodeChildLayout (records, i, side,
				node_stride, children_offset);
			if (child >= 0)
				parents[child] = i;
		}
	for (i = 0; i < num_nodes; i++)
	{
		depth = 1;
		for (node = i; parents[node] >= 0; node = parents[node])
		{
			if (++depth > max_depth)
			{
				error->node = i;
				error->side = error->child = -1;
				free (parents);
				return BSP38_NODE_GRAPH_TOO_DEEP;
			}
		}
	}
	free (parents);
	return BSP38_NODE_GRAPH_OK;
}

static bsp38_node_graph_status_t BSP38_ValidateNodeGraph (
	const byte *records, int num_nodes, int num_leafs,
	bsp38_node_graph_error_t *error)
{
	return BSP38_ValidateNodeGraphLayout (records, num_nodes, num_leafs,
		sizeof (dnode_t), offsetof (dnode_t, children), error);
}

#endif
