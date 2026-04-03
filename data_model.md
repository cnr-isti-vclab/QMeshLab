# Data Model

This application uses a simple data model 
A document is an ordered list of meshes each mesh is a layer. 

For meshes we use the vcglib. We define a vcgMesh that is a specialization of the vcg::tri::TriMesh class 
There are per properties per viewer and properties per document. 
The viewer properties are used to store the visibility of the mesh, the rendering mode the current camera position  , etc. The document properties are used to store the name of the mesh, the path from which it was loaded, etc.

There is a single document and multiple views. The document emits signals when the mesh is added or removed, and the views are connected to these signals to update their content.  

There is a per document log that is used to store the log messages. The log messages are emitted by the document and the views are connected to these signals to update their content.

The bottom part of the main window is a log widget that shows the log messages. The log messages are emitted by the document and eventually the views are connected to these signals to update their content. The log messages are stored in a per document log, so that when a new document is created, the log is cleared.

Methods for writing in the log are provided by the document and a vcg::Callback can be provided for catching the log messages emitted by the vcglib. 

