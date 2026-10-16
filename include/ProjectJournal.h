/*
 * ProjectJournal.h - declaration of class ProjectJournal
 *
 * Copyright (c) 2006-2010 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_PROJECT_JOURNAL_H
#define LMMS_PROJECT_JOURNAL_H

#include <QHash>
#include <QStack>

#include "LmmsTypes.h"
#include "DataFile.h"


namespace lmms
{


class JournallingObject;


//! @warning many parts of this class may be rewritten soon
class ProjectJournal
{
public:
	static const int MAX_UNDO_STATES;

	ProjectJournal();
	virtual ~ProjectJournal() = default;

	void undo();
	void redo();

	bool canUndo() const;
	bool canRedo() const;

	void addJournalCheckPoint( JournallingObject *jo );

	bool isJournalling() const
	{
		return m_journalling;
	}

	void setJournalling( const bool _on )
	{
		m_journalling = _on;
	}

	// alloc new ID and register object _obj to it
	jo_id_t allocID( JournallingObject * _obj );

	// if there's already something known about ID _id, but it is currently
	// unused (e.g. after jouralling object was deleted), register object
	// _obj to this id
	void reallocID( const jo_id_t _id, JournallingObject * _obj );

	// make ID _id unused, but keep all global journalling information
	// (order of journalling entries etc.) referring to _id - needed for
	// restoring a journalling object later
	void freeID( const jo_id_t _id )
	{
		reallocID( _id, nullptr );
	}

	//! hack, not used when saving a file
	static jo_id_t idToSave( jo_id_t id );
	//! hack, not used when loading a savefile
	static jo_id_t idFromSave( jo_id_t id );

	void clearJournal();
	void stopAllJournalling();
	JournallingObject * journallingObject( const jo_id_t _id )
	{
		if( m_joIDs.contains( _id ) )
		{
			return m_joIDs[_id];
		}
		return nullptr;
	}


	void beginJournalGroup( const QString & name = QString(), const QString & groupId = QString() );
	void endJournalGroup();
	void cancelJournalGroup();
	bool isGroupOpen() const { return m_openGroup.has_value(); }
	QString currentUndoGroupId() const;

	class GroupGuard
	{
	public:
		GroupGuard( ProjectJournal * pj, const QString & name = QString(), const QString & groupId = QString() )
			: m_pj( pj )
		{
			if( m_pj ) { m_pj->beginJournalGroup( name, groupId ); }
		}
		~GroupGuard()
		{
			if( m_pj && !m_committed ) { m_pj->cancelJournalGroup(); }
		}
		void commit()
		{
			if( m_pj && !m_committed )
			{
				m_pj->endJournalGroup();
				m_committed = true;
			}
		}
	private:
		ProjectJournal * m_pj;
		bool m_committed{false};
	};

private:
	using JoIdMap = QHash<jo_id_t, JournallingObject*>;

	struct CheckPoint
	{
		CheckPoint( jo_id_t initID = 0, const DataFile& initData = DataFile( DataFile::Type::JournalData ) ) :
			joID( initID ),
			data( initData )
		{
		}
		jo_id_t joID;
		DataFile data;
	} ;

	struct JournalEntry
	{
		QString name;
		QString groupId;
		QVector<CheckPoint> checkpoints;
	};

	using EntryStack = QStack<JournalEntry>;

	JoIdMap m_joIDs;

	EntryStack m_undoEntries;
	EntryStack m_redoEntries;

	std::optional<JournalEntry> m_openGroup;

	bool m_journalling;

} ;


} // namespace lmms

#endif // LMMS_PROJECT_JOURNAL_H
