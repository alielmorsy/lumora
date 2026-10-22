package com.lumora.buildlogic

import com.android.build.api.dsl.ApplicationExtension
import groovy.json.JsonSlurper
import org.gradle.api.GradleException
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.artifacts.VersionCatalogsExtension
import java.io.File

class LumoraAndroidAppPlugin : Plugin<Project> {
    override fun apply(project: Project) {
        project.pluginManager.apply("com.android.application")

        val lumoraJsonFile = project.file("lumora.json")
        if (!lumoraJsonFile.exists()) {
            throw GradleException("lumora.json not found in ${project.projectDir}")
        }

        @Suppress("UNCHECKED_CAST")
        val json = try {
            JsonSlurper().parse(lumoraJsonFile) as? Map<String, Any?>
                ?: throw GradleException("Invalid JSON in ${lumoraJsonFile.absolutePath}")
        } catch (e: Exception) {
            throw GradleException("Failed to parse ${lumoraJsonFile.absolutePath}: ${e.message}", e)
        }

        val id = json["id"] as? String
            ?: throw GradleException("'id' is required in ${lumoraJsonFile.absolutePath}")
        val label = json["label"] as? String
            ?: throw GradleException("'label' is required in ${lumoraJsonFile.absolutePath}")
        val icon = json["icon"] as? String
        val orientation = json["orientation"] as? String ?: "unspecified"
        @Suppress("UNCHECKED_CAST")
        val permissions = (json["permissions"] as? List<*>)?.map { it.toString() } ?: emptyList()
        val versionCode = (json["versionCode"] as? Number)?.toInt() ?: 1
        val versionName = (json["versionName"] as? String) ?: "1.0"

        @Suppress("UNCHECKED_CAST")
        val entry = json["entry"] as? Map<String, Any?>
            ?: throw GradleException("'entry' object is required in ${lumoraJsonFile.absolutePath}")

        if (entry.containsKey("js")) {
            throw GradleException("JS entry not supported yet")
        }

        val entryNative = entry["native"] as? String
        if (entryNative.isNullOrBlank()) {
            throw GradleException("'entry.native' is required in ${lumoraJsonFile.absolutePath}")
        }

        // Set build output directories inside <Android root>
        project.layout.buildDirectory.set(File(project.rootDir, "build/${project.name}"))

        // Depend on :lumora-android library module
        project.dependencies.add("implementation", project.dependencies.project(mapOf("path" to ":lumora-android")))

        val libs = project.rootProject.extensions.getByType(VersionCatalogsExtension::class.java).named("libs")
        val minSdkVer = libs.findVersion("minSdk").map { it.requiredVersion.toInt() }.orElse(30)
        val targetSdkVer = libs.findVersion("targetSdk").map { it.requiredVersion.toInt() }.orElse(36)
        val compileSdkVer = libs.findVersion("compileSdk").map { it.requiredVersion.toInt() }.orElse(36)
        val compileSdkMinorVer = libs.findVersion("compileSdkMinor").map { it.requiredVersion.toInt() }.orElse(1)

        val android = project.extensions.getByType(ApplicationExtension::class.java)

        android.namespace = id

        android.compileSdk {
            version = release(compileSdkVer) {
                minorApiLevel = compileSdkMinorVer
            }
        }

        val appIcon = when {
            icon.isNullOrBlank() -> "@mipmap/ic_launcher"
            icon.startsWith("@") -> icon
            else -> "@mipmap/$icon"
        }

        android.defaultConfig {
            applicationId = id
            minSdk = minSdkVer
            targetSdk = targetSdkVer
            this.versionCode = versionCode
            this.versionName = versionName

            resValue("string", "app_name", label)

            manifestPlaceholders["libName"] = entryNative
            manifestPlaceholders["appLabel"] = label
            manifestPlaceholders["orientation"] = orientation
            manifestPlaceholders["appIcon"] = appIcon

            ndk {
                abiFilters.addAll(listOf("arm64-v8a", "x86_64"))
            }

            externalNativeBuild {
                cmake {
                    targets.add(entryNative)
                    arguments.addAll(
                        listOf(
                            "-DLUMORA_BUILD_TESTS=OFF",
                            "-DLUMORA_BUILD_BENCHMARKS=OFF"
                        )
                    )
                }
            }
        }

        android.buildFeatures {
            prefab = true
            resValues = true
        }

        val rootDir = project.rootDir
        val rootCMake = project.rootDir.parentFile.resolve("CMakeLists.txt")
        val relPath = project.projectDir.toPath().relativize(rootCMake.toPath()).toString().replace('\\', '/')
        android.externalNativeBuild {
            cmake {
                path = project.file(relPath)
                version = "3.31.6"
                buildStagingDirectory = File(rootDir, ".cxx/${project.name}")
            }
        }

        // Optional android/res
        val userRes = project.file("android/res")
        if (userRes.isDirectory) {
            android.sourceSets.getByName("main").res.directories.add(userRes.absolutePath)
        }

        // Setup manifest: overlay permissions and merge optional android/AndroidManifest.xml
        setupManifest(project, android, permissions)
    }

    private fun setupManifest(
        project: Project,
        android: ApplicationExtension,
        permissions: List<String>
    ) {
        val userManifest = project.file("android/AndroidManifest.xml")
        val generatedDir = project.layout.buildDirectory.dir("generated/lumora/manifest").get().asFile
        val generatedManifest = File(generatedDir, "AndroidManifest.xml")

        generatedDir.mkdirs()
        val permElements = permissions.joinToString("\n") { rawPerm ->
            val permName = if (rawPerm.contains('.')) rawPerm else "android.permission.$rawPerm"
            "    <uses-permission android:name=\"$permName\" />"
        }

        val manifestXml = buildString {
            appendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>")
            appendLine("<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"")
            appendLine("    xmlns:tools=\"http://schemas.android.com/tools\">")
            if (permElements.isNotEmpty()) {
                appendLine(permElements)
            }
            appendLine("    <application")
            appendLine("        android:label=\"\${appLabel}\"")
            appendLine("        android:icon=\"\${appIcon}\">")
            appendLine("        <activity")
            appendLine("            android:name=\"com.lumora.android.LumoraActivity\"")
            appendLine("            android:screenOrientation=\"\${orientation}\"")
            appendLine("            tools:replace=\"android:screenOrientation\">")
            appendLine("            <meta-data")
            appendLine("                android:name=\"android.app.lib_name\"")
            appendLine("                android:value=\"\${libName}\"")
            appendLine("                tools:replace=\"android:value\" />")
            appendLine("        </activity>")
            appendLine("    </application>")
            appendLine("</manifest>")
        }
        generatedManifest.writeText(manifestXml)

        if (userManifest.exists()) {
            android.sourceSets.getByName("main").manifest.srcFile(userManifest)
            android.sourceSets.getByName("debug").manifest.srcFile(generatedManifest)
            android.sourceSets.getByName("release").manifest.srcFile(generatedManifest)
        } else {
            android.sourceSets.getByName("main").manifest.srcFile(generatedManifest)
        }
    }
}
