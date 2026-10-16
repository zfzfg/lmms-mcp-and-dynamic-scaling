/*
 * ProjectJournal.cpp - implementation of ProjectJournal
 *
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <cstdlib>
#include <QDomElement>

#include "ProjectJournal.h"
#include "Engine.h"
#include "JournallingObject.h"
#include "lmms_math.h"
#include "Song.h"
#include "AutomationClip.h"

namespace lmms
{

//! Avoid clashes between loaded IDs (have the bit cleared)
//! and newly created IDs (have the bit set)
static const int EO_ID_MSB = 1 << 23;

const int ProjectJournal::MAX_UNDO_STATES = 100; // TODO: make this configurable in settings

ProjectJournal::ProjectJournal() :
	m_joIDs(),
	m_undoEntries(),
	m_redoEntries(),
	m_openGroup( std::nullopt ),
	m_journalling( false )
{
}




void ProjectJournal::beginJournalGroup( const QString & name, const QString & groupId )
{
	JournalEntry entry;
	entry.name = name;
	entry.groupId = groupId.isEmpty() ? QString("undo_%1").arg(fastRand(), 0, 16) : groupId;
	m_openGroup = entry;
}

void ProjectJournal::endJournalGroup()
{
	if( !m_openGroup.has_value() )
	{
		return;
	}

	JournalEntry entry = std::move( m_openGroup.value() );
	m_openGroup = std::nullopt;

	if( !entry.checkpoints.isEmpty() )
	{
		m_redoEntries.clear();
		m_undoEntries.push( std::move( entry ) );
		if( m_undoEntries.size() > MAX_UNDO_STATES )
		{
			m_undoEntries.remove( 0, m_undoEntries.size() - MAX_UNDO_STATES );
		}
	}
}

void ProjectJournal::cancelJournalGroup()
{
	m_openGroup = std::nullopt;
}

QString ProjectJournal::currentUndoGroupId() const
{
	if( !m_undoEntries.isEmpty() )
	{
		return m_undoEntries.top().groupId;
	}
	return QString();
}

void ProjectJournal::undo()
{
	while( !m_undoEntries.isEmpty() )
	{
		JournalEntry entry = m_undoEntries.pop();
		JournalEntry redoEntry;
		redoEntry.name = entry.name;
		redoEntry.groupId = entry.groupId;

		bool anyRestored = false;
		bool hasAutomationClips = false;

		// Save current states of all objects in the group for redo
		for( const CheckPoint & c : entry.checkpoints )
		{
			JournallingObject * jo = m_joIDs.value( c.joID, nullptr );
			if( jo )
			{
				DataFile curState( DataFile::Type::JournalData );
				jo->saveState( curState, curState.content() );
				redoEntry.checkpoints.append( CheckPoint( c.joID, curState ) );
			}
		}

		bool prev = isJournalling();
		setJournalling( false );

		// Restore checkpoints in reverse order
		for( int i = entry.checkpoints.size() - 1; i >= 0; --i )
		{
			const CheckPoint & c = entry.checkpoints.at( i );
			JournallingObject * jo = m_joIDs.value( c.joID, nullptr );
			if( jo )
			{
				jo->restoreState( c.data.content().firstChildElement() );
				anyRestored = true;
				if( !c.data.content().elementsByTagName( "automationclip" ).isEmpty() )
				{
					hasAutomationClips = true;
				}
			}
		}

		setJournalling( prev );

		if( anyRestored )
		{
			m_redoEntries.push( std::move( redoEntry ) );
			Engine::getSong()->setModified();
			if( hasAutomationClips )
			{
				AutomationClip::resolveAllIDs();
			}
			break;
		}
	}
}



void ProjectJournal::redo()
{
	while( !m_redoEntries.isEmpty() )
	{
		JournalEntry entry = m_redoEntries.pop();
		JournalEntry undoEntry;
		undoEntry.name = entry.name;
		undoEntry.groupId = entry.groupId;

		bool anyRestored = false;
		bool hasAutomationClips = false;

		// Save current states for undo
		for( const CheckPoint & c : entry.checkpoints )
		{
			JournallingObject * jo = m_joIDs.value( c.joID, nullptr );
			if( jo )
			{
				DataFile curState( DataFile::Type::JournalData );
				jo->saveState( curState, curState.content() );
				undoEntry.checkpoints.append( CheckPoint( c.joID, curState ) );
			}
		}

		bool prev = isJournalling();
		setJournalling( false );

		// Restore checkpoints in forward order
		for( const CheckPoint & c : entry.checkpoints )
		{
			JournallingObject * jo = m_joIDs.value( c.joID, nullptr );
			if( jo )
			{
				jo->restoreState( c.data.content().firstChildElement() );
				anyRestored = true;
				if( !c.data.content().elementsByTagName( "automationclip" ).isEmpty() )
				{
					hasAutomationClips = true;
				}
			}
		}

		setJournalling( prev );

		if( anyRestored )
		{
			m_undoEntries.push( std::move( undoEntry ) );
			Engine::getSong()->setModified();
			if( hasAutomationClips )
			{
				AutomationClip::resolveAllIDs();
			}
			break;
		}
	}
}

bool ProjectJournal::canUndo() const
{
	return !m_undoEntries.isEmpty();
}

bool ProjectJournal::canRedo() const
{
	return !m_redoEntries.isEmpty();
}



void ProjectJournal::addJournalCheckPoint( JournallingObject *jo )
{
	if( isJournalling() && jo )
	{
		DataFile dataFile( DataFile::Type::JournalData );
		jo->saveState( dataFile, dataFile.content() );
		CheckPoint cp( jo->id(), dataFile );

		if( m_openGroup.has_value() )
		{
			// Check if already captured in this group
			for( const CheckPoint & existing : m_openGroup->checkpoints )
			{
				if( existing.joID == jo->id() )
				{
					return; // Keep original state before mutations began
				}
			}
			m_openGroup->checkpoints.append( std::move( cp ) );
		}
		else
		{
			m_redoEntries.clear();
			JournalEntry entry;
			entry.groupId = QString("undo_%1").arg(fastRand(), 0, 16);
			entry.checkpoints.append( std::move( cp ) );
			m_undoEntries.push( std::move( entry ) );
			if( m_undoEntries.size() > MAX_UNDO_STATES )
			{
				m_undoEntries.remove( 0, m_undoEntries.size() - MAX_UNDO_STATES );
			}
		}
	}
}


jo_id_t ProjectJournal::allocID(JournallingObject* obj)
{
	jo_id_t id;
	for (jo_id_t tid = fastRand(); m_joIDs.contains(id = tid % EO_ID_MSB | EO_ID_MSB); tid++) {}
	m_joIDs[id] = obj;
	return id;
}


void ProjectJournal::reallocID( const jo_id_t _id, JournallingObject * _obj )
{
	//printf("realloc %d %d\n", _id, _obj );
//	if( m_joIDs.contains( _id ) )
	{
		m_joIDs[_id] = _obj;
	}
}




jo_id_t ProjectJournal::idToSave( jo_id_t id )
{
	return id & ~EO_ID_MSB;
}

jo_id_t ProjectJournal::idFromSave( jo_id_t id )
{
	return id | EO_ID_MSB;
}




void ProjectJournal::clearJournal()
{
	m_undoEntries.clear();
	m_redoEntries.clear();
	m_openGroup = std::nullopt;

	for( JoIdMap::Iterator it = m_joIDs.begin(); it != m_joIDs.end(); )
	{
		if( it.value() == nullptr )
		{
			it = m_joIDs.erase( it );
		}
		else
		{
			++it;
		}
	}
}

void ProjectJournal::stopAllJournalling()
{
	for( JoIdMap::Iterator it = m_joIDs.begin(); it != m_joIDs.end(); ++it)
	{
		if( it.value() != nullptr )
		{
			it.value()->setJournalling(false);
		}
	}
	setJournalling(false);
}



} // namespace lmms
