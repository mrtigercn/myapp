#include "StdAfx.h"
#ifndef ACL_PREPARE_COMPILE

#include <stdio.h>
#include "stdlib/acl_mystring.h"
#include "code/acl_xmlcode.h"
#include "xml/acl_xml.h"

#endif

#define	ADDCH	ACL_VSTRING_ADDCH
#define	LEN	ACL_VSTRING_LEN
#define	STR	acl_vstring_str
#define END	acl_vstring_end
#define	STRCPY	acl_vstring_strcpy

#define IS_DOCTYPE(ptr) ((*(ptr) == 'd' || *(ptr) == 'D')  \
	&& (*(ptr + 1) == 'o' || *(ptr + 1) == 'O')  \
	&& (*(ptr + 2) == 'c' || *(ptr + 2) == 'C')  \
	&& (*(ptr + 3) == 't' || *(ptr + 3) == 'T')  \
	&& (*(ptr + 4) == 'y' || *(ptr + 4) == 'Y')  \
	&& (*(ptr + 5) == 'p' || *(ptr + 5) == 'P')  \
	&& (*(ptr + 5) == 'E' || *(ptr + 6) == 'E'))

#define IS_ID(ptr) ((*(ptr) == 'i' || *(ptr) == 'I')  \
	&& (*(ptr + 1) == 'd' || *(ptr + 1) == 'D'))

#define IS_QUOTE(x) ((x) == '\"' || (x) == '\'')
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
#define SKIP_WHILE(cond, ptr) { while(*(ptr) && (cond)) (ptr)++; }
#define SKIP_SPACE(ptr) { while(IS_SPACE(*(ptr))) (ptr)++; }

/* ״̬�����ݽṹ���� */

struct XML_STATUS_MACHINE {
	/**< ״̬�� */
	int   status;

	/**< ״̬���������� */
	const char *(*callback) (ACL_XML*, const char*);
};

static void xml_parse_check_self_closed(ACL_XML *xml)
{
	if ((xml->curr_node->flag & ACL_XML_F_LEAF) == 0) {
		if (acl_xml_tag_leaf(STR(xml->curr_node->ltag))) {
			xml->curr_node->flag |= ACL_XML_F_LEAF;
		}
	}

	if ((xml->curr_node->flag & ACL_XML_F_SELF_CL) == 0) {
		if (xml->curr_node->last_ch == '/'
		    || acl_xml_tag_selfclosed(STR(xml->curr_node->ltag)))
		{
			xml->curr_node->flag |= ACL_XML_F_SELF_CL;
		}
	}
}

static const char *xml_parse_next_left_lt(ACL_XML *xml, const char *data)
{
	SKIP_SPACE(data);
	SKIP_WHILE(*data != '<', data);
	if (*data == 0)
		return data;
	data++;
	xml->curr_node->status = ACL_XML_S_LLT;
	return data;
}

static const char *xml_parse_left_lt(ACL_XML *xml, const char *data)
{
	xml->curr_node->status = ACL_XML_S_LCH;
	return data;
}

static const char *xml_parse_left_ch(ACL_XML *xml, const char *data)
{
	int  ch = *data;

	if (ch == '!') {
		xml->curr_node->meta[0] = ch;
		xml->curr_node->status = ACL_XML_S_LEM;
		data++;
	} else if (ch == '?') {
		xml->curr_node->meta[0] = ch;
		xml->curr_node->flag |= ACL_XML_F_META_QM;
		xml->curr_node->status = ACL_XML_S_MTAG;
		data++;
	} else
		xml->curr_node->status = ACL_XML_S_LTAG;

	return data;
}

static const char *xml_parse_left_em(ACL_XML *xml, const char *data)
{
	if (*data == '-') {
		if (xml->curr_node->meta[1] != '-')
			xml->curr_node->meta[1] = '-';
		else if (xml->curr_node->meta[2] != '-') {
			xml->curr_node->meta[0] = 0;
			xml->curr_node->meta[1] = 0;
			xml->curr_node->meta[2] = 0;
			xml->curr_node->flag |= ACL_XML_F_META_CM;
			xml->curr_node->status = ACL_XML_S_MCMT;
		}

		data++;
	} else {
		if (xml->curr_node->meta[1] == '-') {
			ADDCH(xml->curr_node->ltag, '-');
			xml->curr_node->meta[1] = 0;
		}

		xml->curr_node->flag |= ACL_XML_F_META_EM;
		xml->curr_node->status = ACL_XML_S_MTAG;
	}

	ACL_VSTRING_TERMINATE(xml->curr_node->ltag);

	return data;
}

static const char *xml_parse_meta_tag(ACL_XML *xml, const char *data)
{
	int   ch;

	while ((ch = *data) != 0) {
		data++;
		if (IS_SPACE(ch) || ch == '>') {
			xml->curr_node->status = ACL_XML_S_MTXT;
			break;
		}
		ADDCH(xml->curr_node->ltag, ch);
	}

	ACL_VSTRING_TERMINATE(xml->curr_node->ltag);
	return data;
}

static const char *xml_meta_attr_name(ACL_XML_ATTR *attr, const char *data)
{
	int   ch;

	while ((ch = *data) != 0) {
		if (ch == '=') {
			data++;
			ACL_VSTRING_TERMINATE(attr->name);
			break;
		}
		if (!IS_SPACE(ch))
			ADDCH(attr->name, ch);
		data++;
	}

	return data;
}

static const char *xml_meta_attr_value(ACL_XML_ATTR *attr, const char *data)
{
	int   ch;

	SKIP_SPACE(data);
	if (IS_QUOTE(*data))
		attr->quote = *data++;

	if (*data == 0)
		return data;

	while ((ch = *data) != 0) {
		if (attr->quote) {
			if (ch == attr->quote) {
				data++;
				break;
			}
			ADDCH(attr->value, ch);
		} else if (IS_SPACE(ch)) {
			data++;
			break;
		} else
			ADDCH(attr->value, ch);
		data++;
	}

	ACL_VSTRING_TERMINATE(attr->value);
	return data;
}

static void xml_meta_attr(ACL_XML_NODE *node)
{
	ACL_XML_ATTR *attr;
	const char *ptr;
	int   ch;

	ptr = STR(node->text);
	SKIP_SPACE(ptr);	/* �Թ� ' ', '\t' */

	if (*ptr == 0)
		return;

	while ((ch = *ptr) != 0) {
		attr = acl_xml_attr_alloc(node);
		ptr = xml_meta_attr_name(attr, ptr);
		if (*ptr == 0)
			break;
		ptr = xml_meta_attr_value(attr, ptr);
		if (*ptr == 0)
			break;
	}
}

static const char *xml_parse_meta_text(ACL_XML *xml, const char *data)
{
	int   ch;

	if (LEN(xml->curr_node->text) == 0)
		SKIP_SPACE(data);

	if (*data == 0)
		return data;

	while ((ch = *data) != 0) {
		if (xml->curr_node->quote) {
			if (ch == xml->curr_node->quote)
				xml->curr_node->quote = 0;
			ADDCH(xml->curr_node->text, ch);
		} else if (IS_QUOTE(ch)) {
			if (xml->curr_node->quote == 0)
				xml->curr_node->quote = ch;
			ADDCH(xml->curr_node->text, ch);
		} else if (ch == '<') {
			xml->curr_node->nlt++;
			ADDCH(xml->curr_node->text, ch);
		} else if (ch != '>') {
			ADDCH(xml->curr_node->text, ch);
		} else if (xml->curr_node->nlt == 0) {
			char *last;
			int off;

			data++;
			xml->curr_node->status = ACL_XML_S_MEND;
			if ((xml->curr_node->flag & ACL_XML_F_META_QM) == 0)
				break;

			last = acl_vstring_end(xml->curr_node->text) - 1;
			if (last < STR(xml->curr_node->text) || *last != '?')
				break;

			off = (int) ACL_VSTRING_LEN(xml->curr_node->text) - 1;
			if (off == 0)
				break;

			ACL_VSTRING_AT_OFFSET(xml->curr_node->text, off);
			ACL_VSTRING_TERMINATE(xml->curr_node->text);

			xml_meta_attr(xml->curr_node);
			break;
		} else {
			xml->curr_node->nlt--;
			ADDCH(xml->curr_node->text, ch);
		}

		data++;
	}

	ACL_VSTRING_TERMINATE(xml->curr_node->text);
	return data;
}

static const char *xml_parse_meta_comment(ACL_XML *xml, const char *data)
{
	int   ch;

	if (LEN(xml->curr_node->text) == 0)
		SKIP_SPACE(data);

	if (*data == 0)
		return data;

	while ((ch = *data) != 0) {
		if (xml->curr_node->quote) {
			if (ch == xml->curr_node->quote)
				xml->curr_node->quote = 0;
			else
				ADDCH(xml->curr_node->text, ch);
		} else if (IS_QUOTE(ch)) {
			if (xml->curr_node->quote == 0)
				xml->curr_node->quote = ch;
			else
				ADDCH(xml->curr_node->text, ch);
		} else if (ch == '<') {
			xml->curr_node->nlt++;
			ADDCH(xml->curr_node->text, ch);
		} else if (ch == '>') {
			if (xml->curr_node->nlt == 0
				&& xml->curr_node->meta[0] == '-'
				&& xml->curr_node->meta[1] == '-')
			{
				data++;
				xml->curr_node->status = ACL_XML_S_MEND;
				break;
			}

			xml->curr_node->nlt--;
			ADDCH(xml->curr_node->text, ch);
		} else if (xml->curr_node->nlt > 0) {
			ADDCH(xml->curr_node->text, ch);
		} else if (ch == '-') {
			if (xml->curr_node->meta[0] != '-')
				xml->curr_node->meta[0] = '-';
			else if (xml->curr_node->meta[1] != '-')
				xml->curr_node->meta[1] = '-';
		} else {
			if (xml->curr_node->meta[0] == '-') {
				ADDCH(xml->curr_node->text, '-');
				xml->curr_node->meta[0] = 0;
			}
			if (xml->curr_node->meta[1] == '-') {
				ADDCH(xml->curr_node->text, '-');
				xml->curr_node->meta[1] = 0;
			}
			ADDCH(xml->curr_node->text, ch);
		}

		data++;
	}

	ACL_VSTRING_TERMINATE(xml->curr_node->text);
	return data;
}

static const char *xml_parse_meta_end(ACL_XML *xml, const char *data)
{
	/* meta ��ǩ���Թر����ͣ�ֱ�������ұ� '>' ����λ�� */
	xml->curr_node->status = ACL_XML_S_RGT;
	return data;
}

static const char *xml_parse_left_tag(ACL_XML *xml, const char *data)
{
	int   ch;

	if (LEN(xml->curr_node->ltag) == 0)
		SKIP_SPACE(data);

	if (*data == 0)
		return data;

	while ((ch = *data) != 0) {
		data++;

		if (ch == '>') {
			xml_parse_check_self_closed(xml);

			if ((xml->curr_node->flag & ACL_XML_F_SELF_CL)
				&& xml->curr_node->last_ch == '/')
			{
				acl_vstring_truncate(xml->curr_node->ltag,
					LEN(xml->curr_node->ltag) - 1);
				xml->curr_node->status = ACL_XML_S_RGT;
			} else
				xml->curr_node->status = ACL_XML_S_LGT;
			break;
		} else if (IS_SPACE(ch)) {
			xml->curr_node->status = ACL_XML_S_ATTR;
			xml->curr_node->last_ch = ch;
			break;
		} else {
			ADDCH(xml->curr_node->ltag, ch);
			xml->curr_node->last_ch = ch;
		}
	}

	ACL_VSTRING_TERMINATE(xml->curr_node->ltag);
	return data;
}

static const char *xml_parse_attr(ACL_XML *xml, const char *data)
{
	int   ch;
	ACL_XML_ATTR *attr = xml->curr_node->curr_attr;

	if (attr == NULL || LEN(attr->name) == 0) {
		SKIP_SPACE(data);	/* �Թ� ' ', '\t' */
		SKIP_WHILE(*data == '=', data);
	}

	if (*data == 0)
		return data;

	if (*data == '>') {
		xml_parse_check_self_closed(xml);

		if ((xml->curr_node->flag & ACL_XML_F_SELF_CL)
			&& xml->curr_node->last_ch == '/')
		{
			xml->curr_node->status = ACL_XML_S_RGT;
		} else
			xml->curr_node->status = ACL_XML_S_LGT;

		xml->curr_node->curr_attr = NULL;
		data++;
		return data;
	}

	xml->curr_node->last_ch = *data;
	if (*data == '/') {
		data++;

		/* �˴����غ�ᴥ���������ٴα����ã�����һ���ֽ�Ϊ '>' ʱ��
		 * ����ͨ������ xml_parse_check_self_closed ����Ƿ�Ϊ�Է��
		 * ��ǩ: "/>"
		 */
		return data;
	}

	if (attr == NULL) {
		attr = acl_xml_attr_alloc(xml->curr_node);
		xml->curr_node->curr_attr = attr;
	}

	while ((ch = *data) != 0) {
		data++;
		xml->curr_node->last_ch = ch;
		if (ch == '=') {
			xml->curr_node->status = ACL_XML_S_AVAL;
			break;
		}
		if (!IS_SPACE(ch))
			ADDCH(attr->name, ch);
	}

	ACL_VSTRING_TERMINATE(attr->name);
	return data;
}

static const char *xml_parse_attr_val(ACL_XML *xml, const char *data)
{
	int   ch;
	ACL_XML_ATTR *attr = xml->curr_node->curr_attr;

	if (LEN(attr->value) == 0 && !attr->quote) {
		SKIP_SPACE(data);
		if (IS_QUOTE(*data))
			attr->quote = *data++;
	}

	if (*data == 0)
		return data;

	while ((ch = *data) != 0) {
		data++;

		if (attr->quote) {
			if (ch == attr->quote) {
				xml->curr_node->status = ACL_XML_S_ATTR;
				xml->curr_node->last_ch = ch;
				break;
			}
			ADDCH(attr->value, ch);
			xml->curr_node->last_ch = ch;
		} else if (ch == '>') {
			xml_parse_check_self_closed(xml);

			if ((xml->curr_node->flag & ACL_XML_F_SELF_CL)
				&& xml->curr_node->last_ch == '/')
			{
				acl_vstring_truncate(attr->value,
					LEN(attr->value) - 1);
				xml->curr_node->status = ACL_XML_S_RGT;
			} else
				xml->curr_node->status = ACL_XML_S_LGT;
			break;
		} else if (IS_SPACE(ch)) {
			xml->curr_node->status = ACL_XML_S_ATTR;
			xml->curr_node->last_ch = ch;
			break;
		} else {
			ADDCH(attr->value, ch);
			xml->curr_node->last_ch = ch;
		}
	}

	ACL_VSTRING_TERMINATE(attr->value);

	if (xml->curr_node->status != ACL_XML_S_AVAL) {
		if (LEN(attr->value) > 0 && xml->decode_buf != NULL) {
			ACL_VSTRING_RESET(xml->decode_buf);
			acl_xml_decode(STR(attr->value), xml->decode_buf);
			if (LEN(xml->decode_buf) > 0)
				STRCPY(attr->value, STR(xml->decode_buf));
		}

		/* ���ñ�ǩID��ӳ������ϣ���У��Ա��ڿ��ٲ�ѯ */
		if (IS_ID(STR(attr->name)) && LEN(attr->value) > 0) {
			const char *ptr = STR(attr->value);

			/* ��ֹ�ظ�ID���������� */
			if (acl_htable_find(xml->id_table, ptr) == NULL) {
				acl_htable_enter(xml->id_table, ptr, attr);

				/* �������Ա������ϣ����Żḳ�ڽڵ�� id */
				xml->curr_node->id = attr->value;
			}
		}

		/* ���뽫�ýڵ�ĵ�ǰ���Զ����ÿգ��Ա��ڼ�������ʱ
		 * ���Դ����µ����Զ���
		 */
		xml->curr_node->curr_attr = NULL;
	}

	return data;
}

static const char *xml_parse_left_gt(ACL_XML *xml, const char *data)
{
	xml->curr_node->last_ch = 0;
	xml->curr_node->status = ACL_XML_S_TXT;
	return data;
}

static const char *xml_parse_text(ACL_XML *xml, const char *data)
{
	int   ch;

	if (LEN(xml->curr_node->text) == 0)
		SKIP_SPACE(data);

	if (*data == 0)
		return data;

	while ((ch = *data) != 0) {
		data++;

		if (ch == '<') {
			xml->curr_node->status = ACL_XML_S_RLT;
			break;
		}

		ADDCH(xml->curr_node->text, ch);
	}

	ACL_VSTRING_TERMINATE(xml->curr_node->text);

	if (xml->curr_node->status != ACL_XML_S_RLT)
		return data;

	if (LEN(xml->curr_node->text) == 0 || xml->decode_buf == NULL)
		return data;

	ACL_VSTRING_RESET(xml->decode_buf);
	acl_xml_decode(STR(xml->curr_node->text), xml->decode_buf);
	if (LEN(xml->decode_buf) > 0)
		STRCPY(xml->curr_node->text, STR(xml->decode_buf));

	return data;
}

static const char *xml_parse_right_lt(ACL_XML *xml, const char *data)
{
	ACL_XML_NODE *node;

	SKIP_SPACE(data);
	if (*data == 0)
		return data;

	if (*data == '/') {
		data++;
		xml->curr_node->status = ACL_XML_S_RTAG;

		return data;
	} else if ((xml->curr_node->flag & ACL_XML_F_LEAF)) {
		ADDCH(xml->curr_node->text, '<');
		ADDCH(xml->curr_node->text, *data);
		ACL_VSTRING_TERMINATE(xml->curr_node->text);
		xml->curr_node->status = ACL_XML_S_TXT;
		data++;

		return data;
	}

	/* ˵�������˵�ǰ�ڵ���ӽڵ� */

	/* �������õ�ǰ�ڵ�״̬���Ա����䱻�ӽڵ㵯��ʱ�����ҵ� "</" */
	xml->curr_node->status = ACL_XML_S_TXT;

	/* �����µ��ӽڵ㣬�������������ǰ�ڵ���ӽڵ㼯���� */

	node = acl_xml_node_alloc(xml);
	acl_xml_node_add_child(xml->curr_node, node);
	node->depth = xml->curr_node->depth + 1;
	if (node->depth > xml->depth)
		xml->depth = node->depth;
	xml->curr_node = node;
	xml->curr_node->status = ACL_XML_S_LLT;

	return data;
}

/* ��Ϊ�ø��ڵ���ʵΪҶ�ڵ㣬������Ҫ���¸����ڸ�α���ڵ��
 * �ӽڵ�����ֵ����Ӧ���α���ڵ���ͬ
 */ 
static void update_children_depth(ACL_XML_NODE *parent)
{
	ACL_ITER  iter;
	ACL_XML_NODE *child;

	acl_foreach(iter, parent) {
		child = (ACL_XML_NODE*) iter.data;
		child->depth = parent->depth;
		update_children_depth(child);
	}
}

/* �������ұ�ǩ��ͬ�ĸ��ڵ� */
static int search_match_node(ACL_XML *xml)
{
	ACL_XML_NODE *parent, *node;
	ACL_ARRAY *nodes = acl_array_create(10);
	ACL_ITER iter;

	parent = acl_xml_node_parent(xml->curr_node);
	if (parent != xml->root)
		acl_array_append(nodes, xml->curr_node);

	while (parent != xml->root) {
		if (acl_strcasecmp(STR(xml->curr_node->rtag),
			STR(parent->ltag)) == 0)
		{
			acl_vstring_strcpy(parent->rtag,
				STR(xml->curr_node->rtag));
			ACL_VSTRING_RESET(xml->curr_node->rtag);
			ACL_VSTRING_TERMINATE(xml->curr_node->rtag);
			parent->status = ACL_XML_S_RGT;
			xml->curr_node = parent;
			break;
		}

		acl_array_append(nodes, parent);

		parent = acl_xml_node_parent(parent);
	}

	if (parent == xml->root) {
		acl_array_free(nodes, NULL);
		return 0;
	}

	acl_foreach_reverse(iter, nodes) {
		node = (ACL_XML_NODE*) iter.data;
		acl_ring_detach(&node->node);
		node->flag |= ACL_XML_F_LEAF;
		node->depth = parent->depth + 1;
		update_children_depth(node);
		acl_xml_node_add_child(parent, node);
	}

	acl_array_free(nodes, NULL);

	return 1;
}

static const char *xml_parse_right_tag(ACL_XML *xml, const char *data)
{
	int   ch;
	ACL_XML_NODE *curr_node = xml->curr_node;

	/* after: "</" */

	if (LEN(curr_node->rtag) == 0)
		SKIP_SPACE(data);

	if (*data == 0)
		return data;

	while ((ch = *data) != 0) {
		data++;

		if (ch == '>') {
			curr_node->status = ACL_XML_S_RGT;
			break;
		}

		if (!IS_SPACE(ch))
			ADDCH(curr_node->rtag, ch);
	}

	ACL_VSTRING_TERMINATE(curr_node->rtag);

	if (curr_node->status != ACL_XML_S_RGT)
		return data;

	if (acl_strcasecmp(STR(curr_node->ltag), STR(curr_node->rtag)) != 0) {
		int   ret;

		if ((xml->flag & ACL_XML_FLAG_IGNORE_SLASH))
			ret = search_match_node(xml);
		else
			ret = 0;

		if (ret == 0) {
			/* ����ڵ��ǩ���뿪ʼ��ǩ����ƥ�䣬
			 * ����Ҫ����Ѱ�������Ľ�����ǩ
			 */ 
			acl_vstring_strcat(curr_node->text,
				STR(curr_node->rtag));
			ACL_VSTRING_RESET(curr_node->rtag);
			ACL_VSTRING_TERMINATE(curr_node->rtag);

			/* �������õ�ǰ�ڵ�״̬���Ա���������ҵ� "</" */
			curr_node->status = ACL_XML_S_TXT;
		}
	}

	return data;
}

static const char *xml_parse_right_gt(ACL_XML *xml, const char *data)
{
	/* ��ǰ�ڵ������ϣ���Ҫ������ǰ�ڵ�ĸ��ڵ�������� */
	ACL_XML_NODE *parent = acl_xml_node_parent(xml->curr_node);

	if (parent == xml->root) {
		if ((xml->curr_node->flag & ACL_XML_F_META) == 0)
			xml->root_cnt++;
		xml->curr_node = NULL;
	} else
		xml->curr_node = parent;

	return data;
}

static struct XML_STATUS_MACHINE status_tab[] = {
	{ ACL_XML_S_NXT,  xml_parse_next_left_lt        },
	{ ACL_XML_S_LLT,  xml_parse_left_lt             },
	{ ACL_XML_S_LGT,  xml_parse_left_gt             },
	{ ACL_XML_S_LCH,  xml_parse_left_ch             },
	{ ACL_XML_S_LEM,  xml_parse_left_em             },
	{ ACL_XML_S_LTAG, xml_parse_left_tag            },
	{ ACL_XML_S_RLT,  xml_parse_right_lt            },
	{ ACL_XML_S_RGT,  xml_parse_right_gt            },
	{ ACL_XML_S_RTAG, xml_parse_right_tag           },
	{ ACL_XML_S_ATTR, xml_parse_attr                },
	{ ACL_XML_S_AVAL, xml_parse_attr_val            },
	{ ACL_XML_S_TXT,  xml_parse_text                },
	{ ACL_XML_S_MTAG, xml_parse_meta_tag            },
	{ ACL_XML_S_MTXT, xml_parse_meta_text           },
	{ ACL_XML_S_MCMT, xml_parse_meta_comment	},
	{ ACL_XML_S_MEND, xml_parse_meta_end            },
};

const char *acl_xml_update(ACL_XML *xml, const char *data)
{
	if (data == NULL || *data == 0)
		return data;

	if (!(xml->flag & ACL_XML_FLAG_MULTI_ROOT) && xml->root_cnt > 0)
		return data;

	/* XML ������״̬��ѭ���������� */

	while (*data) {
		if (xml->curr_node == NULL) {
			if (!(xml->flag & ACL_XML_FLAG_MULTI_ROOT)
				&& xml->root_cnt > 0)
			{
				break;
			}

			SKIP_SPACE(data);
			if (*data == 0)
				break;
			xml->curr_node = acl_xml_node_alloc(xml);
			acl_xml_node_add_child(xml->root, xml->curr_node);
			xml->curr_node->depth = xml->root->depth + 1;
			if (xml->curr_node->depth > xml->depth)
				xml->depth = xml->curr_node->depth;
		}
		data = status_tab[xml->curr_node->status].callback(xml, data);
	}

	return data;
}