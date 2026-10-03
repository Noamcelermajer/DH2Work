#!/usr/bin/env python3
"""Host regression tests using small provider doubles, not Android/ART emulation."""
from pathlib import Path
import subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
JDK=next((ROOT.parent/'toolchains').glob('jdk-17*'))
STUBS={
'android/database/Cursor.java': '''package android.database; public interface Cursor extends AutoCloseable {
int getColumnIndexOrThrow(String n); boolean moveToNext(); long getLong(int i); String getString(int i); int getCount(); void close(); }''',
'android/database/MatrixCursor.java': '''package android.database; import java.util.*;
public class MatrixCursor implements Cursor {
private final String[] names; private final List<Object[]> rows=new ArrayList<>(); private int position=-1; public boolean closed;
public MatrixCursor(String[] n){names=n.clone();} public void addRow(Object[] r){rows.add(r);}
public int getColumnIndexOrThrow(String n){for(int i=0;i<names.length;i++)if(names[i].equals(n))return i;throw new IllegalArgumentException(n);}
public boolean moveToNext(){return ++position<rows.size();} public long getLong(int i){return ((Number)rows.get(position)[i]).longValue();}
public String getString(int i){return (String)rows.get(position)[i];} public int getCount(){return rows.size();} public void close(){closed=true;} }''',
'android/content/Context.java': '''package android.content; import java.io.File;
public abstract class Context {public abstract ContentResolver getContentResolver(); public File getExternalFilesDir(String t){return null;}}''',
'android/content/ContentResolver.java': '''package android.content; import android.database.Cursor;
public abstract class ContentResolver {public abstract Cursor query(Object uri,String[] projection,String selection,String[] args,String sort);}''',
'android/database/sqlite/SQLiteException.java': '''package android.database.sqlite; public class SQLiteException extends RuntimeException {}''',
'android/provider/MediaStore.java': '''package android.provider; public class MediaStore { public static class Audio {public static class Playlists {public static final Object EXTERNAL_CONTENT_URI=new Object();}}}''',
'android/util/Log.java': '''package android.util; public class Log {public static int w(String tag,String text,Throwable t){return 0;}}''',
'MediaQueriesTest.java': '''import android.content.*; import android.database.*; import android.database.sqlite.SQLiteException; import local.dh2.compat.MediaQueries;
public class MediaQueriesTest {
 static int passed;
 static class Provider extends ContentResolver {
  Cursor cursor; RuntimeException failure; int queries;
  public Cursor query(Object uri,String[] projection,String selection,String[] args,String sort){
   queries++; if(!java.util.Arrays.equals(projection,new String[]{"_id","name"}))throw new AssertionError("Invalid projection");
   if(failure!=null)throw failure; return cursor;
  }
 }
 static Cursor run(Provider p){return MediaQueries.queryPlaylists(new Context(){public ContentResolver getContentResolver(){return p;}});}
 static void check(boolean v){if(!v)throw new AssertionError();}
 static void empty(Provider p){try(Cursor c=run(p)){check(c!=null && c.getCount()==0 && !c.moveToNext());check(p.queries==1);}passed++;}
 public static void main(String[] ignored){
  Provider p=new Provider();MatrixCursor source=new MatrixCursor(new String[]{"_id","name"});source.addRow(new Object[]{7L,"First"});source.addRow(new Object[]{11L,"Second"});p.cursor=source;
  try(Cursor c=run(p)){check(c.getCount()==2);check(c.moveToNext()&&c.getLong(0)==7&&c.getString(1).equals("First"));check(c.moveToNext()&&c.getLong(0)==11&&c.getString(1).equals("Second"));check(source.closed);}passed++;
  empty(new Provider());
  for(RuntimeException e:new RuntimeException[]{new SecurityException("denied"),new IllegalArgumentException("Invalid column"),new SQLiteException()}){Provider f=new Provider();f.failure=e;empty(f);}
  Provider missing=new Provider();MatrixCursor malformed=new MatrixCursor(new String[]{"_id"});missing.cursor=malformed;empty(missing);check(malformed.closed);
  Provider mid=new Provider();MatrixCursor partial=new MatrixCursor(new String[]{"_id","name"}){int calls; public boolean moveToNext(){if(++calls==2)throw new SQLiteException();return super.moveToNext();}};partial.addRow(new Object[]{1L,"Before failure"});mid.cursor=partial;empty(mid);check(partial.closed);
  System.out.println("PASS "+passed+" media-query regression cases: explicit projection, rows, null provider, denied access, invalid schema, database error and cursor cleanup. Host doubles only; Android device retest required.");
 }
}'''
}
with tempfile.TemporaryDirectory(prefix='dh2-media-query-') as temp:
    root=Path(temp)
    for name,content in STUBS.items():
        path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(content)
    source=ROOT/'guest-java/local/dh2/compat/MediaQueries.java'
    subprocess.run([str(JDK/'bin/javac'),'-d',temp,*map(str,root.rglob('*.java')),str(source)],check=True)
    subprocess.run([str(JDK/'bin/java'),'-cp',temp,'MediaQueriesTest'],check=True)
