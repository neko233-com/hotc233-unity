using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Hotc233.Editor.Settings
{
    public class Hotc233SettingProvider : SettingsProvider
    {
        private SerializedObject _serializedObject;
        private SerializedProperty _enable;
        private SerializedProperty _useGlobalIl2cpp;
        private SerializedProperty _logLanguage;
        private SerializedProperty _hotUpdateAssemblyDefinitions;
        private SerializedProperty _hotUpdateAssemblies;
        private SerializedProperty _preserveHotUpdateAssemblies;
        private SerializedProperty _hotUpdateDllCompileOutputRootDir;
        private SerializedProperty _externalHotUpdateAssemblyDirs;
        private SerializedProperty _strippedAOTDllOutputRootDir;
        private SerializedProperty _patchAOTAssemblies;
        private SerializedProperty _outputLinkFile;
        private SerializedProperty _outputAOTGenericReferenceFile;
        private SerializedProperty _maxGenericReferenceIteration;
        private SerializedProperty _maxMethodBridgeGenericIteration;
        private SerializedProperty _enableFullGenericSharing;
        private SerializedProperty _enableMetadataOptimization;
        private SerializedProperty _enableStandardInterpreterOptimization;
        private SerializedProperty _enableOfflineInstructionOptimization;
        private SerializedProperty _enableHotfix;
        private SerializedProperty _enableHotReloadWorkflow;
        private SerializedProperty _enableCodeProtection;
        private SerializedProperty _enableAccessControl;
        private SerializedProperty _payloadProtectionKeyId;
        private SerializedProperty _payloadManifestPath;

        public Hotc233SettingProvider() : base("Project/Hotc233 Settings", SettingsScope.Project) { }

        public override void OnActivate(string searchContext, VisualElement rootElement)
        {
            InitGUI();
        }

        private void InitGUI()
        {
            var setting = Hotc233Settings.LoadOrCreate();
            _serializedObject?.Dispose();
            _serializedObject = new SerializedObject(setting);
            _enable = _serializedObject.FindProperty("enable");
            _useGlobalIl2cpp = _serializedObject.FindProperty("useGlobalIl2cpp");
            _logLanguage = _serializedObject.FindProperty("logLanguage");
            _hotUpdateAssemblyDefinitions = _serializedObject.FindProperty("hotUpdateAssemblyDefinitions");
            _hotUpdateAssemblies = _serializedObject.FindProperty("hotUpdateAssemblies");
            _preserveHotUpdateAssemblies = _serializedObject.FindProperty("preserveHotUpdateAssemblies");
            _hotUpdateDllCompileOutputRootDir = _serializedObject.FindProperty("hotUpdateDllCompileOutputRootDir");
            _externalHotUpdateAssemblyDirs = _serializedObject.FindProperty("externalHotUpdateAssembliyDirs");
            _strippedAOTDllOutputRootDir = _serializedObject.FindProperty("strippedAOTDllOutputRootDir");
            _patchAOTAssemblies = _serializedObject.FindProperty("patchAOTAssemblies");
            _outputLinkFile = _serializedObject.FindProperty("outputLinkFile");
            _outputAOTGenericReferenceFile = _serializedObject.FindProperty("outputAOTGenericReferenceFile");
            _maxGenericReferenceIteration = _serializedObject.FindProperty("maxGenericReferenceIteration");
            _maxMethodBridgeGenericIteration = _serializedObject.FindProperty("maxMethodBridgeGenericIteration");
            _enableFullGenericSharing = _serializedObject.FindProperty("enableFullGenericSharing");
            _enableMetadataOptimization = _serializedObject.FindProperty("enableMetadataOptimization");
            _enableStandardInterpreterOptimization = _serializedObject.FindProperty("enableStandardInterpreterOptimization");
            _enableOfflineInstructionOptimization = _serializedObject.FindProperty("enableOfflineInstructionOptimization");
            _enableHotfix = _serializedObject.FindProperty("enableHotfix");
            _enableHotReloadWorkflow = _serializedObject.FindProperty("enableHotReloadWorkflow");
            _enableCodeProtection = _serializedObject.FindProperty("enableCodeProtection");
            _enableAccessControl = _serializedObject.FindProperty("enableAccessControl");
            _payloadProtectionKeyId = _serializedObject.FindProperty("payloadProtectionKeyId");
            _payloadManifestPath = _serializedObject.FindProperty("payloadManifestPath");
        }

        public override void OnGUI(string searchContext)
        {
            if (_serializedObject == null || !_serializedObject.targetObject)
            {
                InitGUI();
            }
            _serializedObject.Update();
            EditorGUI.BeginChangeCheck();
            EditorGUILayout.PropertyField(_enable);
            EditorGUILayout.PropertyField(_useGlobalIl2cpp);
            EditorGUILayout.PropertyField(_logLanguage);
            EditorGUILayout.PropertyField(_hotUpdateAssemblyDefinitions);
            EditorGUILayout.PropertyField(_hotUpdateAssemblies);
            EditorGUILayout.PropertyField(_preserveHotUpdateAssemblies);
            EditorGUILayout.PropertyField(_hotUpdateDllCompileOutputRootDir);
            EditorGUILayout.PropertyField(_externalHotUpdateAssemblyDirs);
            EditorGUILayout.PropertyField(_strippedAOTDllOutputRootDir);
            EditorGUILayout.PropertyField(_patchAOTAssemblies);
            EditorGUILayout.PropertyField(_outputLinkFile);
            EditorGUILayout.PropertyField(_outputAOTGenericReferenceFile);
            EditorGUILayout.PropertyField(_maxGenericReferenceIteration);
            EditorGUILayout.PropertyField(_maxMethodBridgeGenericIteration);
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Unity 2022+ Business Compatibility", EditorStyles.boldLabel);
            EditorGUILayout.PropertyField(_enableFullGenericSharing);
            EditorGUILayout.PropertyField(_enableMetadataOptimization);
            EditorGUILayout.PropertyField(_enableStandardInterpreterOptimization);
            EditorGUILayout.PropertyField(_enableOfflineInstructionOptimization);
            EditorGUILayout.PropertyField(_enableHotfix);
            EditorGUILayout.PropertyField(_enableHotReloadWorkflow);
            EditorGUILayout.PropertyField(_enableCodeProtection);
            EditorGUILayout.PropertyField(_enableAccessControl);
            EditorGUILayout.PropertyField(_payloadProtectionKeyId);
            EditorGUILayout.PropertyField(_payloadManifestPath);
            if (EditorGUI.EndChangeCheck())
            {
                _serializedObject.ApplyModifiedProperties();
                Hotc233Settings.Save();
            }
        }

        public override void OnDeactivate()
        {
            base.OnDeactivate();
            Hotc233Settings.Save();
        }

        static Hotc233SettingProvider s_provider;

        [SettingsProvider]
        public static SettingsProvider CreateMyCustomSettingsProvider()
        {
            if (s_provider == null)
            {
                s_provider = new Hotc233SettingProvider();
            }
            return s_provider;
        }
    }
}
