package {{androidPackage}}

import com.lumora.android.modules.LumoraModule
import com.lumora.android.modules.ModuleDefinition

class {{Name}}Module : LumoraModule() {
    override fun definition() = ModuleDefinition {
        Name("{{Name}}")
        Function("hello") { "Hello from Kotlin" }
    }
}
