using Microsoft.Data.Sqlite;

namespace Mosaic.Server.Data;

internal static class SqliteCommands
{
    /// <summary>
    /// Creates a command enlisted in <paramref name="transaction"/> when one is open.
    /// Microsoft.Data.Sqlite rejects commands that omit the connection's pending transaction.
    /// </summary>
    public static SqliteCommand CreateCommand(this SqliteConnection connection, string sql, SqliteTransaction? transaction = null)
    {
        var command = connection.CreateCommand();
        command.CommandText = sql;
        command.Transaction = transaction;
        return command;
    }
}
