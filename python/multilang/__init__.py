"""
multilang — reusable string/translation storage library.

Public API:
    db_connector(backend, **credentials)   Open a connection to the given backend.
    retrieve_data(conn, string_id, language_id, context='')
    insert_data(conn, string_id, language_id, content, ...)
    search_data(conn, query, mode='natural', ...)
"""

from .connector import db_connector
from .strings import retrieve_data, insert_data, search_data
from .validation import ValidationError

__all__ = ["db_connector", "retrieve_data", "insert_data", "search_data", "ValidationError"]
