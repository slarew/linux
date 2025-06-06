// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_QUOTA_DEFS_H__
#define __XFS_QUOTA_DEFS_H__

/*
 * Quota definitions shared between user and kernel source trees.
 */

/*
 * Even though users may not have quota limits occupying all 64-bits,
 * they may need 64-bit accounting. Hence, 64-bit quota-counters,
 * and quota-limits. This is a waste in the common case, but hey ...
 */
typedef uint64_t	xfs_qcnt_t;

typedef uint8_t		xfs_dqtype_t;

#define XFS_DQTYPE_STRINGS \
	{ XFS_DQTYPE_USER,	"USER" }, \
	{ XFS_DQTYPE_PROJ,	"PROJ" }, \
	{ XFS_DQTYPE_GROUP,	"GROUP" }, \
	{ XFS_DQTYPE_BIGTIME,	"BIGTIME" }

/*
 * flags for q_flags field in the dquot.
 */
#define XFS_DQFLAG_DIRTY	(1u << 0)	/* dquot is dirty */
#define XFS_DQFLAG_FREEING	(1u << 1)	/* dquot is being torn down */

#define XFS_DQFLAG_STRINGS \
	{ XFS_DQFLAG_DIRTY,	"DIRTY" }, \
	{ XFS_DQFLAG_FREEING,	"FREEING" }

/*
 * We have the possibility of all three quota types being active at once, and
 * hence free space modification requires modification of all three current
 * dquots in a single transaction. For this case we need to have a reservation
 * of at least 3 dquots.
 *
 * However, a chmod operation can change both UID and GID in a single
 * transaction, resulting in requiring {old, new} x {uid, gid} dquots to be
 * modified. Hence for this case we need to reserve space for at least 4 dquots.
 *
 * And in the worst case, there's a rename operation that can be modifying up to
 * 4 inodes with dquots attached to them. In reality, the only inodes that can
 * have their dquots modified are the source and destination directory inodes
 * due to directory name creation and removal. That can require space allocation
 * and/or freeing on both directory inodes, and hence all three dquots on each
 * inode can be modified. And if the directories are world writeable, all the
 * dquots can be unique and so 6 dquots can be modified....
 *
 * And, of course, we also need to take into account the dquot log format item
 * used to describe each dquot.
 */
#define XFS_DQUOT_LOGRES	\
	((sizeof(struct xfs_dq_logformat) + sizeof(struct xfs_disk_dquot)) * 6)

#define XFS_IS_QUOTA_ON(mp)		((mp)->m_qflags & XFS_ALL_QUOTA_ACCT)
#define XFS_IS_UQUOTA_ON(mp)		((mp)->m_qflags & XFS_UQUOTA_ACCT)
#define XFS_IS_PQUOTA_ON(mp)		((mp)->m_qflags & XFS_PQUOTA_ACCT)
#define XFS_IS_GQUOTA_ON(mp)		((mp)->m_qflags & XFS_GQUOTA_ACCT)
#define XFS_IS_UQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_UQUOTA_ENFD)
#define XFS_IS_GQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_GQUOTA_ENFD)
#define XFS_IS_PQUOTA_ENFORCED(mp)	((mp)->m_qflags & XFS_PQUOTA_ENFD)

/*
 * Flags to tell various functions what to do. Not all of these are meaningful
 * to a single function. None of these XFS_QMOPT_* flags are meant to have
 * persistent values (ie. their values can and will change between versions)
 */
#define XFS_QMOPT_UQUOTA	(1u << 0) /* user dquot requested */
#define XFS_QMOPT_GQUOTA	(1u << 1) /* group dquot requested */
#define XFS_QMOPT_PQUOTA	(1u << 2) /* project dquot requested */
#define XFS_QMOPT_FORCE_RES	(1u << 3) /* ignore quota limits */
#define XFS_QMOPT_SBVERSION	(1u << 4) /* change superblock version num */

/*
 * flags to xfs_trans_mod_dquot to indicate which field needs to be
 * modified.
 */
#define XFS_QMOPT_RES_REGBLKS	(1u << 7)
#define XFS_QMOPT_RES_RTBLKS	(1u << 8)
#define XFS_QMOPT_BCOUNT	(1u << 9)
#define XFS_QMOPT_ICOUNT	(1u << 10)
#define XFS_QMOPT_RTBCOUNT	(1u << 11)
#define XFS_QMOPT_DELBCOUNT	(1u << 12)
#define XFS_QMOPT_DELRTBCOUNT	(1u << 13)
#define XFS_QMOPT_RES_INOS	(1u << 14)

/*
 * flags for dqalloc.
 */
#define XFS_QMOPT_INHERIT	(1u << 31)

#define XFS_QMOPT_FLAGS \
	{ XFS_QMOPT_UQUOTA,		"UQUOTA" }, \
	{ XFS_QMOPT_PQUOTA,		"PQUOTA" }, \
	{ XFS_QMOPT_FORCE_RES,		"FORCE_RES" }, \
	{ XFS_QMOPT_SBVERSION,		"SBVERSION" }, \
	{ XFS_QMOPT_GQUOTA,		"GQUOTA" }, \
	{ XFS_QMOPT_INHERIT,		"INHERIT" }, \
	{ XFS_QMOPT_RES_REGBLKS,	"RES_REGBLKS" }, \
	{ XFS_QMOPT_RES_RTBLKS,		"RES_RTBLKS" }, \
	{ XFS_QMOPT_BCOUNT,		"BCOUNT" }, \
	{ XFS_QMOPT_ICOUNT,		"ICOUNT" }, \
	{ XFS_QMOPT_RTBCOUNT,		"RTBCOUNT" }, \
	{ XFS_QMOPT_DELBCOUNT,		"DELBCOUNT" }, \
	{ XFS_QMOPT_DELRTBCOUNT,	"DELRTBCOUNT" }, \
	{ XFS_QMOPT_RES_INOS,		"RES_INOS" }

/*
 * flags to xfs_trans_mod_dquot.
 */
#define XFS_TRANS_DQ_RES_BLKS	XFS_QMOPT_RES_REGBLKS
#define XFS_TRANS_DQ_RES_RTBLKS	XFS_QMOPT_RES_RTBLKS
#define XFS_TRANS_DQ_RES_INOS	XFS_QMOPT_RES_INOS
#define XFS_TRANS_DQ_BCOUNT	XFS_QMOPT_BCOUNT
#define XFS_TRANS_DQ_DELBCOUNT	XFS_QMOPT_DELBCOUNT
#define XFS_TRANS_DQ_ICOUNT	XFS_QMOPT_ICOUNT
#define XFS_TRANS_DQ_RTBCOUNT	XFS_QMOPT_RTBCOUNT
#define XFS_TRANS_DQ_DELRTBCOUNT XFS_QMOPT_DELRTBCOUNT


#define XFS_QMOPT_QUOTALL	\
		(XFS_QMOPT_UQUOTA | XFS_QMOPT_PQUOTA | XFS_QMOPT_GQUOTA)
#define XFS_QMOPT_RESBLK_MASK	(XFS_QMOPT_RES_REGBLKS | XFS_QMOPT_RES_RTBLKS)


extern xfs_failaddr_t xfs_dquot_verify(struct xfs_mount *mp,
		struct xfs_disk_dquot *ddq, xfs_dqid_t id);
extern xfs_failaddr_t xfs_dqblk_verify(struct xfs_mount *mp,
		struct xfs_dqblk *dqb, xfs_dqid_t id);
extern int xfs_calc_dquots_per_chunk(unsigned int nbblks);
extern void xfs_dqblk_repair(struct xfs_mount *mp, struct xfs_dqblk *dqb,
		xfs_dqid_t id, xfs_dqtype_t type);

struct xfs_dquot;
time64_t xfs_dquot_from_disk_ts(struct xfs_disk_dquot *ddq,
		__be32 dtimer);
__be32 xfs_dquot_to_disk_ts(struct xfs_dquot *ddq, time64_t timer);

static inline const char *
xfs_dqinode_path(xfs_dqtype_t type)
{
	switch (type) {
	case XFS_DQTYPE_USER:
		return "user";
	case XFS_DQTYPE_GROUP:
		return "group";
	case XFS_DQTYPE_PROJ:
		return "project";
	}

	ASSERT(0);
	return NULL;
}

static inline enum xfs_metafile_type
xfs_dqinode_metafile_type(xfs_dqtype_t type)
{
	switch (type) {
	case XFS_DQTYPE_USER:
		return XFS_METAFILE_USRQUOTA;
	case XFS_DQTYPE_GROUP:
		return XFS_METAFILE_GRPQUOTA;
	case XFS_DQTYPE_PROJ:
		return XFS_METAFILE_PRJQUOTA;
	}

	ASSERT(0);
	return XFS_METAFILE_UNKNOWN;
}

unsigned int xfs_dqinode_sick_mask(xfs_dqtype_t type);

int xfs_dqinode_load(struct xfs_trans *tp, struct xfs_inode *dp,
		xfs_dqtype_t type, struct xfs_inode **ipp);
int xfs_dqinode_metadir_create(struct xfs_inode *dp, xfs_dqtype_t type,
		struct xfs_inode **ipp);
int xfs_dqinode_metadir_link(struct xfs_inode *dp, xfs_dqtype_t type,
		struct xfs_inode *ip);
int xfs_dqinode_mkdir_parent(struct xfs_mount *mp, struct xfs_inode **dpp);
int xfs_dqinode_load_parent(struct xfs_trans *tp, struct xfs_inode **dpp);

#endif	/* __XFS_QUOTA_H__ */
