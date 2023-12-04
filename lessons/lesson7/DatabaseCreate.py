import sqlite3

def create_database(db_name):
    # Connect to SQLite database (or create if it doesn't exist)
    conn = sqlite3.connect(db_name)

    # Create a cursor object using the cursor() method
    cursor = conn.cursor()

    # SQL command to create a table in the database
    sql_command = """
    CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT NOT NULL,
        password TEXT NOT NULL);"""

    # Execute the SQL command
    cursor.execute(sql_command)

    # Commit the changes and close the connection
    conn.commit()
    conn.close()

    print(f"Database '{db_name}' initialized successfully.")

# Replace 'your_database_name.db' with your desired database name
create_database("database.db")
