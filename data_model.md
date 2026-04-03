# Data Model

This application uses a simple data model 
A document is an ordered list of meshes; each mesh is called a layer. 

For meshes we use the vcglib. We define a vcgMesh that is a specialization of the vcg::tri::TriMesh class 
There are per properties per viewer and properties per document. 
The viewer properties are used to store the visibility of the mesh, the rendering mode the current camera position, etc. The document properties are used to store the name of the mesh, the path from which it was loaded, etc.

There is a single document and multiple views. The document emits signals when the mesh is added or removed, and the views are connected to these signals to update their content.  

There is a per document log that is used to store the log messages. The log messages are emitted by the document and the views are connected to these signals to update their content.

The bottom part of the main window is a log widget that shows the log messages. The log messages are emitted by the document and eventually the views are connected to these signals to update their content. The log messages are stored in a per document log, so that when a new document is created, the log is cleared.

Methods for writing in the log are provided by the document and a vcg::Callback can be provided for catching the log messages emitted by the vcglib. 

Meshes can store appearance information in different ways:
- per vertex: color, normal, quality.
- per face: color, normal, quality.
- per mesh: color, normal, quality.



## Rendering 
The rendering is done using the QRhi. The rendering is done in a separate thread, so that the UI is not blocked. The rendering thread is created when the first view is created, and it is destroyed when the last view is destroyed. The rendering thread is responsible for rendering the meshes and emitting signals when the rendering is done. The views are connected to these signals to update their content. The rendering thread is also responsible for handling the camera position and the rendering mode. The camera position and the rendering mode are stored in the viewer properties, so that they are preserved when the view is destroyed

We support different rendering modes:
- point cloud: only the vertices are rendered as points.
- wireframe: only the edges are rendered as lines.
- flat: the faces are rendered with flat shading.
- smooth: the faces are rendered with smooth shading.


