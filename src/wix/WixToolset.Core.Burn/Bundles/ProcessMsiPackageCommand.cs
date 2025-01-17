// Copyright (c) .NET Foundation and contributors. All rights reserved. Licensed under the Microsoft Reciprocal License. See LICENSE.TXT file in the project root for full license information.

namespace WixToolset.Core.Burn.Bundles
{
    using System;
    using System.Collections.Generic;
    using System.Globalization;
    using System.IO;
    using System.Linq;
    using WixToolset.Data;
    using WixToolset.Extensibility;
    using WixToolset.Extensibility.Services;
    using WixToolset.Data.Symbols;
    using WixToolset.Data.WindowsInstaller;
    using WixToolset.Extensibility.Data;
    using WixToolset.Core.Native.Msi;

    /// <summary>
    /// Initializes package state from the MSI contents.
    /// </summary>
    internal class ProcessMsiPackageCommand
    {
        private const string PropertySqlQuery = "SELECT `Value` FROM `Property` WHERE `Property` = ?";

        public ProcessMsiPackageCommand(IServiceProvider serviceProvider, IEnumerable<IBurnBackendBinderExtension> backendExtensions, IntermediateSection section, PackageFacade facade, Dictionary<string, WixBundlePayloadSymbol> packagePayloads)
        {
            this.Messaging = serviceProvider.GetService<IMessaging>();
            this.BackendHelper = serviceProvider.GetService<IBackendHelper>();
            this.PathResolver = serviceProvider.GetService<IPathResolver>();

            this.BackendExtensions = backendExtensions;

            this.PackagePayloads = packagePayloads;
            this.Section = section;
            this.Facade = facade;
        }

        private IMessaging Messaging { get; }

        private IBackendHelper BackendHelper { get; }

        private IPathResolver PathResolver { get; }

        private IEnumerable<IBurnBackendBinderExtension> BackendExtensions { get; }

        private Dictionary<string, WixBundlePayloadSymbol> PackagePayloads { get; }

        private PackageFacade Facade { get; }

        private IntermediateSection Section { get; }

        /// <summary>
        /// Processes the MSI packages to add properties and payloads from the MSI packages.
        /// </summary>
        public void Execute()
        {
            var packagePayload = this.PackagePayloads[this.Facade.PackageSymbol.PayloadRef];

            var msiPackage = (WixBundleMsiPackageSymbol)this.Facade.SpecificPackageSymbol;

            var sourcePath = packagePayload.SourceFile.Path;
            var longNamesInImage = false;
            var compressed = false;
            try
            {
                using (var db = new Database(sourcePath, OpenDatabase.ReadOnly))
                {
                    // Read data out of the msi database...
                    using (var sumInfo = new SummaryInformation(db))
                    {
                        var fileAndElevateFlags = sumInfo.GetNumericProperty(SummaryInformation.Package.FileAndElevatedFlags);
                        var platformsAndLanguages = sumInfo.GetProperty(SummaryInformation.Package.PlatformsAndLanguages);

                        // 1 is the Word Count summary information stream bit that means
                        // the MSI uses short file names when set. We care about long file
                        // names so check when the bit is not set.

                        longNamesInImage = 0 == (fileAndElevateFlags & 1);

                        // 2 is the Word Count summary information stream bit that means
                        // files are compressed in the MSI by default when the bit is set.
                        compressed = 2 == (fileAndElevateFlags & 2);

                        // 8 is the Word Count summary information stream bit that means
                        // "Elevated privileges are not required to install this package."
                        // in MSI 4.5 and below, if this bit is 0, elevation is required.
                        var perMachine = (0 == (fileAndElevateFlags & 8));
                        var x64 = platformsAndLanguages.Contains("x64");

                        this.Facade.PackageSymbol.PerMachine = perMachine ? YesNoDefaultType.Yes : YesNoDefaultType.No;
                        this.Facade.PackageSymbol.Win64 = x64;
                    }

                    string packageName = null;
                    string packageDescription = null;
                    string allusers = null;
                    string fastInstall = null;
                    string systemComponent = null;

                    using (var view = db.OpenView(PropertySqlQuery))
                    {
                        packageName = ProcessMsiPackageCommand.GetProperty(view, "ProductName");
                        packageDescription = ProcessMsiPackageCommand.GetProperty(view, "ARPCOMMENTS");
                        allusers = ProcessMsiPackageCommand.GetProperty(view, "ALLUSERS");
                        fastInstall = ProcessMsiPackageCommand.GetProperty(view, "MSIFASTINSTALL");
                        systemComponent = ProcessMsiPackageCommand.GetProperty(view, "ARPSYSTEMCOMPONENT");

                        msiPackage.ProductCode = ProcessMsiPackageCommand.GetProperty(view, "ProductCode");
                        msiPackage.UpgradeCode = ProcessMsiPackageCommand.GetProperty(view, "UpgradeCode");
                        msiPackage.Manufacturer = ProcessMsiPackageCommand.GetProperty(view, "Manufacturer");
                        msiPackage.ProductLanguage = Convert.ToInt32(ProcessMsiPackageCommand.GetProperty(view, "ProductLanguage"), CultureInfo.InvariantCulture);
                        msiPackage.ProductVersion = ProcessMsiPackageCommand.GetProperty(view, "ProductVersion");
                    }

                    if (!this.BackendHelper.IsValidFourPartVersion(msiPackage.ProductVersion))
                    {
                        // not a proper .NET version (e.g., five fields); can we get a valid four-part version number?
                        string version = null;
                        var versionParts = msiPackage.ProductVersion.Split('.');
                        var count = versionParts.Length;
                        if (0 < count)
                        {
                            version = versionParts[0];
                            for (var i = 1; i < 4 && i < count; ++i)
                            {
                                version = String.Concat(version, ".", versionParts[i]);
                            }
                        }

                        if (!String.IsNullOrEmpty(version) && this.BackendHelper.IsValidFourPartVersion(version))
                        {
                            this.Messaging.Write(WarningMessages.VersionTruncated(this.Facade.PackageSymbol.SourceLineNumbers, msiPackage.ProductVersion, sourcePath, version));
                            msiPackage.ProductVersion = version;
                        }
                        else
                        {
                            this.Messaging.Write(ErrorMessages.InvalidProductVersion(this.Facade.PackageSymbol.SourceLineNumbers, msiPackage.ProductVersion, sourcePath));
                        }
                    }

                    if (String.IsNullOrEmpty(this.Facade.PackageSymbol.CacheId))
                    {
                        this.Facade.PackageSymbol.CacheId = String.Format("{0}v{1}", msiPackage.ProductCode, msiPackage.ProductVersion);
                    }

                    if (String.IsNullOrEmpty(this.Facade.PackageSymbol.DisplayName))
                    {
                        this.Facade.PackageSymbol.DisplayName = packageName;
                    }

                    if (String.IsNullOrEmpty(this.Facade.PackageSymbol.Description))
                    {
                        this.Facade.PackageSymbol.Description = packageDescription;
                    }

                    if (String.IsNullOrEmpty(this.Facade.PackageSymbol.Version))
                    {
                        this.Facade.PackageSymbol.Version = msiPackage.ProductVersion;
                    }

                    var payloadNames = this.GetPayloadTargetNames();

                    var msiPropertyNames = this.GetMsiPropertyNames(packagePayload.Id.Id);

                    this.SetPerMachineAppropriately(allusers, msiPackage, sourcePath);

                    // Ensure the MSI package is appropriately marked visible or not.
                    this.SetPackageVisibility(systemComponent, msiPackage, msiPropertyNames);

                    // Unless the MSI or setup code overrides the default, set MSIFASTINSTALL for best performance.
                    if (!String.IsNullOrEmpty(fastInstall))
                    {
                        this.AddMsiProperty(msiPackage, "MSIFASTINSTALL", "7");
                    }

                    this.CreateRelatedPackages(db);

                    // If feature selection is enabled, represent the Feature table in the manifest.
                    if ((msiPackage.Attributes & WixBundleMsiPackageAttributes.EnableFeatureSelection) == WixBundleMsiPackageAttributes.EnableFeatureSelection)
                    {
                        this.CreateMsiFeatures(db);
                    }

                    // Add all external cabinets as package payloads.
                    this.ImportExternalCabinetAsPayloads(db, packagePayload, payloadNames);

                    // Add all external files as package payloads and calculate the total install size as the rollup of
                    // File table's sizes.
                    this.Facade.PackageSymbol.InstallSize = this.ImportExternalFileAsPayloadsAndReturnInstallSize(db, packagePayload, longNamesInImage, compressed, payloadNames);

                    // Add all dependency providers from the MSI.
                    this.ImportDependencyProviders(db, msiPackage);
                }
            }
            catch (MsiException e)
            {
                this.Messaging.Write(ErrorMessages.UnableToReadPackageInformation(this.Facade.PackageSymbol.SourceLineNumbers, sourcePath, e.Message));
            }
        }

        private ISet<string> GetPayloadTargetNames()
        {
            var payloadNames = this.PackagePayloads.Values.Select(p => p.Name);

            return new HashSet<string>(payloadNames, StringComparer.OrdinalIgnoreCase);
        }

        private ISet<string> GetMsiPropertyNames(string packageId)
        {
            var properties = this.Section.Symbols.OfType<WixBundleMsiPropertySymbol>()
                .Where(p => p.PackageRef == packageId)
                .Select(p => p.Name);

            return new HashSet<string>(properties, StringComparer.Ordinal);
        }

        private void SetPerMachineAppropriately(string allusers, WixBundleMsiPackageSymbol msiPackage, string sourcePath)
        {
            if (msiPackage.ForcePerMachine)
            {
                if (YesNoDefaultType.No == this.Facade.PackageSymbol.PerMachine)
                {
                    this.Messaging.Write(WarningMessages.PerUserButForcingPerMachine(this.Facade.PackageSymbol.SourceLineNumbers, sourcePath));
                    this.Facade.PackageSymbol.PerMachine = YesNoDefaultType.Yes; // ensure that we think the package is per-machine.
                }

                // Force ALLUSERS=1 via the MSI command-line.
                this.AddMsiProperty(msiPackage, "ALLUSERS", "1");
            }
            else
            {
                if (String.IsNullOrEmpty(allusers))
                {
                    // Not forced per-machine and no ALLUSERS property, flip back to per-user.
                    if (YesNoDefaultType.Yes == this.Facade.PackageSymbol.PerMachine)
                    {
                        this.Messaging.Write(WarningMessages.ImplicitlyPerUser(this.Facade.PackageSymbol.SourceLineNumbers, sourcePath));
                        this.Facade.PackageSymbol.PerMachine = YesNoDefaultType.No;
                    }
                }
                else if (allusers.Equals("1", StringComparison.Ordinal))
                {
                    if (YesNoDefaultType.No == this.Facade.PackageSymbol.PerMachine)
                    {
                        this.Messaging.Write(ErrorMessages.PerUserButAllUsersEquals1(this.Facade.PackageSymbol.SourceLineNumbers, sourcePath));
                    }
                }
                else if (allusers.Equals("2", StringComparison.Ordinal))
                {
                    this.Messaging.Write(WarningMessages.DiscouragedAllUsersValue(this.Facade.PackageSymbol.SourceLineNumbers, sourcePath, (YesNoDefaultType.Yes == this.Facade.PackageSymbol.PerMachine) ? "machine" : "user"));
                }
                else
                {
                    this.Messaging.Write(ErrorMessages.UnsupportedAllUsersValue(this.Facade.PackageSymbol.SourceLineNumbers, sourcePath, allusers));
                }
            }
        }

        private void SetPackageVisibility(string systemComponent, WixBundleMsiPackageSymbol msiPackage, ISet<string> msiPropertyNames)
        {
            // If the authoring specifically added "ARPSYSTEMCOMPONENT", don't do it again.
            if (!msiPropertyNames.Contains("ARPSYSTEMCOMPONENT"))
            {
                var alreadyVisible = String.IsNullOrEmpty(systemComponent);
                var visible = (this.Facade.PackageSymbol.Attributes & WixBundlePackageAttributes.Visible) == WixBundlePackageAttributes.Visible;

                // If not already set to the correct visibility.
                if (alreadyVisible != visible)
                {
                    this.AddMsiProperty(msiPackage, "ARPSYSTEMCOMPONENT", visible ? String.Empty : "1");
                }
            }
        }

        private void CreateRelatedPackages(Database db)
        {
            // Represent the Upgrade table as related packages.
            if (db.TableExists("Upgrade"))
            {
                using (var view = db.OpenExecuteView("SELECT `UpgradeCode`, `VersionMin`, `VersionMax`, `Language`, `Attributes` FROM `Upgrade`"))
                {
                    foreach (var record in view.Records)
                    {
                        var recordAttributes = record.GetInteger(5);

                        var attributes = WixBundleRelatedPackageAttributes.None;
                        attributes |= (recordAttributes & WindowsInstallerConstants.MsidbUpgradeAttributesOnlyDetect) == WindowsInstallerConstants.MsidbUpgradeAttributesOnlyDetect ? WixBundleRelatedPackageAttributes.OnlyDetect : 0;
                        attributes |= (recordAttributes & WindowsInstallerConstants.MsidbUpgradeAttributesVersionMinInclusive) == WindowsInstallerConstants.MsidbUpgradeAttributesVersionMinInclusive ? WixBundleRelatedPackageAttributes.MinInclusive : 0;
                        attributes |= (recordAttributes & WindowsInstallerConstants.MsidbUpgradeAttributesVersionMaxInclusive) == WindowsInstallerConstants.MsidbUpgradeAttributesVersionMaxInclusive ? WixBundleRelatedPackageAttributes.MaxInclusive : 0;
                        attributes |= (recordAttributes & WindowsInstallerConstants.MsidbUpgradeAttributesLanguagesExclusive) == WindowsInstallerConstants.MsidbUpgradeAttributesLanguagesExclusive ? 0 : WixBundleRelatedPackageAttributes.LangInclusive;

                        this.Section.AddSymbol(new WixBundleRelatedPackageSymbol(this.Facade.PackageSymbol.SourceLineNumbers)
                        {
                            PackageRef = this.Facade.PackageId,
                            RelatedId = record.GetString(1),
                            MinVersion = record.GetString(2),
                            MaxVersion = record.GetString(3),
                            Languages = record.GetString(4),
                            Attributes = attributes,
                        });
                    }
                }
            }
        }

        private void CreateMsiFeatures(Database db)
        {
            if (db.TableExists("Feature"))
            {
                using (var allFeaturesView = db.OpenExecuteView("SELECT * FROM `Feature`"))
                using (var featureView = db.OpenView("SELECT `Component_` FROM `FeatureComponents` WHERE `Feature_` = ?"))
                using (var componentView = db.OpenView("SELECT `FileSize` FROM `File` WHERE `Component_` = ?"))
                {
                    using (var featureRecord = new Record(1))
                    using (var componentRecord = new Record(1))
                    {
                        foreach (var allFeaturesResultRecord in allFeaturesView.Records)
                        {
                            var featureName = allFeaturesResultRecord.GetString(1);

                            // Calculate the Feature size.
                            featureRecord.SetString(1, featureName);
                            featureView.Execute(featureRecord);

                            // Loop over all the components for the feature to calculate the size of the feature.
                            long size = 0;
                            foreach (var componentResultRecord in featureView.Records)
                            {
                                var component = componentResultRecord.GetString(1);
                                componentRecord.SetString(1, component);
                                componentView.Execute(componentRecord);

                                foreach (var fileResultRecord in componentView.Records)
                                {
                                    var fileSize = fileResultRecord.GetString(1);
                                    size += Convert.ToInt32(fileSize, CultureInfo.InvariantCulture.NumberFormat);
                                }
                            }

                            this.Section.AddSymbol(new WixBundleMsiFeatureSymbol(this.Facade.PackageSymbol.SourceLineNumbers, new Identifier(AccessModifier.Section, this.Facade.PackageId, featureName))
                            {
                                PackageRef = this.Facade.PackageId,
                                Name = featureName,
                                Parent = allFeaturesResultRecord.GetString(2),
                                Title = allFeaturesResultRecord.GetString(3),
                                Description = allFeaturesResultRecord.GetString(4),
                                Display = allFeaturesResultRecord.GetInteger(5),
                                Level = allFeaturesResultRecord.GetInteger(6),
                                Directory = allFeaturesResultRecord.GetString(7),
                                Attributes = allFeaturesResultRecord.GetInteger(8),
                                Size = size
                            });
                        }
                    }
                }
            }
        }

        private void ImportExternalCabinetAsPayloads(Database db, WixBundlePayloadSymbol packagePayload, ISet<string> payloadNames)
        {
            if (db.TableExists("Media"))
            {
                using (var view = db.OpenExecuteView("SELECT `Cabinet` FROM `Media`"))
                {
                    foreach (var cabinetRecord in view.Records)
                    {
                        var cabinet = cabinetRecord.GetString(1);

                        if (!String.IsNullOrEmpty(cabinet) && !cabinet.StartsWith("#", StringComparison.Ordinal))
                        {
                            // If we didn't find the Payload as an existing child of the package, we need to
                            // add it.  We expect the file to exist on-disk in the same relative location as
                            // the MSI expects to find it...
                            var cabinetName = Path.Combine(Path.GetDirectoryName(packagePayload.Name), cabinet);

                            if (!payloadNames.Contains(cabinetName))
                            {
                                var generatedId = this.BackendHelper.GenerateIdentifier("cab", packagePayload.Id.Id, cabinet);
                                var payloadSourceFile = this.ResolveRelatedFile(packagePayload.SourceFile.Path, packagePayload.UnresolvedSourceFile, cabinet, "Cabinet", this.Facade.PackageSymbol.SourceLineNumbers);

                                this.Section.AddSymbol(new WixGroupSymbol(this.Facade.PackageSymbol.SourceLineNumbers)
                                {
                                    ParentType = ComplexReferenceParentType.Package,
                                    ParentId = this.Facade.PackageId,
                                    ChildType = ComplexReferenceChildType.Payload,
                                    ChildId = generatedId
                                });

                                this.Section.AddSymbol(new WixBundlePayloadSymbol(this.Facade.PackageSymbol.SourceLineNumbers, new Identifier(AccessModifier.Section, generatedId))
                                {
                                    Name = cabinetName,
                                    SourceFile = new IntermediateFieldPathValue { Path = payloadSourceFile },
                                    Compressed = packagePayload.Compressed,
                                    UnresolvedSourceFile = cabinetName,
                                    ContainerRef = packagePayload.ContainerRef,
                                    DownloadUrl = packagePayload.DownloadUrl,
                                    Packaging = packagePayload.Packaging,
                                    ParentPackagePayloadRef = packagePayload.Id.Id,
                                });
                            }
                        }
                    }
                }
            }
        }

        private long ImportExternalFileAsPayloadsAndReturnInstallSize(Database db, WixBundlePayloadSymbol packagePayload, bool longNamesInImage, bool compressed, ISet<string> payloadNames)
        {
            long size = 0;

            if (db.TableExists("Component") && db.TableExists("Directory") && db.TableExists("File"))
            {
                var directories = new Dictionary<string, IResolvedDirectory>();

                // Load up the directory hash table so we will be able to resolve source paths
                // for files in the MSI database.
                using (var view = db.OpenExecuteView("SELECT `Directory`, `Directory_Parent`, `DefaultDir` FROM `Directory`"))
                {
                    foreach (var record in view.Records)
                    {
                        var sourceName = this.BackendHelper.GetMsiFileName(record.GetString(3), true, longNamesInImage);

                        var resolvedDirectory = this.BackendHelper.CreateResolvedDirectory(record.GetString(2), sourceName);

                        directories.Add(record.GetString(1), resolvedDirectory);
                    }
                }

                // Resolve the source paths to external files and add each file size to the total
                // install size of the package.
                using (var view = db.OpenExecuteView("SELECT `Directory_`, `File`, `FileName`, `File`.`Attributes`, `FileSize` FROM `Component`, `File` WHERE `Component`.`Component`=`File`.`Component_`"))
                {
                    foreach (var record in view.Records)
                    {
                        // If the file is explicitly uncompressed or the MSI is uncompressed and the file is not
                        // explicitly marked compressed then this is an external file.
                        var compressionBit = record.GetInteger(4);
                        if (WindowsInstallerConstants.MsidbFileAttributesNoncompressed == (compressionBit & WindowsInstallerConstants.MsidbFileAttributesNoncompressed) ||
                            (!compressed && 0 == (compressionBit & WindowsInstallerConstants.MsidbFileAttributesCompressed)))
                        {
                            var fileSourcePath = this.PathResolver.GetFileSourcePath(directories, record.GetString(1), record.GetString(3), compressed, longNamesInImage);
                            var name = Path.Combine(Path.GetDirectoryName(packagePayload.Name), fileSourcePath);

                            if (!payloadNames.Contains(name))
                            {
                                var generatedId = this.BackendHelper.GenerateIdentifier("f", packagePayload.Id.Id, record.GetString(2));
                                var payloadSourceFile = this.ResolveRelatedFile(packagePayload.SourceFile.Path, packagePayload.UnresolvedSourceFile, fileSourcePath, "File", this.Facade.PackageSymbol.SourceLineNumbers);

                                this.Section.AddSymbol(new WixGroupSymbol(this.Facade.PackageSymbol.SourceLineNumbers)
                                {
                                    ParentType = ComplexReferenceParentType.Package,
                                    ParentId = this.Facade.PackageId,
                                    ChildType = ComplexReferenceChildType.Payload,
                                    ChildId = generatedId
                                });

                                this.Section.AddSymbol(new WixBundlePayloadSymbol(this.Facade.PackageSymbol.SourceLineNumbers, new Identifier(AccessModifier.Section, generatedId))
                                {
                                    Name = name,
                                    SourceFile = new IntermediateFieldPathValue { Path = payloadSourceFile },
                                    Compressed = packagePayload.Compressed,
                                    UnresolvedSourceFile = name,
                                    ContainerRef = packagePayload.ContainerRef,
                                    DownloadUrl = packagePayload.DownloadUrl,
                                    Packaging = packagePayload.Packaging,
                                    ParentPackagePayloadRef = packagePayload.Id.Id,
                                });
                            }
                        }

                        size += record.GetInteger(5);
                    }
                }
            }

            return size;
        }

        private void AddMsiProperty(WixBundleMsiPackageSymbol msiPackage, string name, string value)
        {
            this.Section.AddSymbol(new WixBundleMsiPropertySymbol(msiPackage.SourceLineNumbers, new Identifier(AccessModifier.Section, msiPackage.Id.Id, name))
            {
                PackageRef = msiPackage.Id.Id,
                Name = name,
                Value = value,
            });
        }

        private void ImportDependencyProviders(Database db, WixBundleMsiPackageSymbol msiPackage)
        {
            this.ImportDependencyProvidersFromTable(db, msiPackage, "WixDependencyProvider");
            this.ImportDependencyProvidersFromTable(db, msiPackage, "Wix4DependencyProvider");
        }

        private void ImportDependencyProvidersFromTable(Database db, WixBundleMsiPackageSymbol msiPackage, string tableName)
        {
            if (db.TableExists(tableName))
            {
                using (var view = db.OpenExecuteView($"SELECT `WixDependencyProvider`, `ProviderKey`, `Version`, `DisplayName`, `Attributes` FROM `{tableName}`"))
                {
                    foreach (var record in view.Records)
                    {
                        var id = new Identifier(AccessModifier.Section, this.BackendHelper.GenerateIdentifier("dep", msiPackage.Id.Id, record.GetString(1)));

                        // Import the provider key and attributes.
                        this.Section.AddSymbol(new WixDependencyProviderSymbol(msiPackage.SourceLineNumbers, id)
                        {
                            ParentRef = msiPackage.Id.Id,
                            ProviderKey = record.GetString(2),
                            Version = record.GetString(3) ?? msiPackage.ProductVersion,
                            DisplayName = record.GetString(4) ?? this.Facade.PackageSymbol.DisplayName,
                            Attributes = WixDependencyProviderAttributes.ProvidesAttributesImported | (WixDependencyProviderAttributes)record.GetInteger(5),
                        });
                    }
                }
            }
        }

        private string ResolveRelatedFile(string resolvedSource, string unresolvedSource, string relatedSource, string type, SourceLineNumber sourceLineNumbers)
        {
            var checkedPaths = new List<string>();

            foreach (var extension in this.BackendExtensions)
            {
                var resolved = extension.ResolveRelatedFile(unresolvedSource, relatedSource, type, sourceLineNumbers);

                if (resolved?.CheckedPaths != null)
                {
                    checkedPaths.AddRange(resolved.CheckedPaths);
                }

                if (!String.IsNullOrEmpty(resolved?.Path))
                {
                    return resolved?.Path;
                }
            }

            var resolvedPath = Path.Combine(Path.GetDirectoryName(resolvedSource), relatedSource);

            if (!File.Exists(resolvedPath))
            {
                checkedPaths.Add(resolvedPath);
                this.Messaging.Write(ErrorMessages.FileNotFound(sourceLineNumbers, resolvedPath, type, checkedPaths));
            }

            return resolvedPath;
        }

        private static string GetProperty(View view, string property)
        {
            using (var queryRecord = new Record(1))
            {
                queryRecord[1] = property;

                view.Execute(queryRecord);

                using (var record = view.Fetch())
                {
                    return record?.GetString(1);
                }
            }
        }
    }
}
